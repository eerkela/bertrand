#ifndef BERTRAND_STATIC_STRING_H
#define BERTRAND_STATIC_STRING_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/union.h"
#include "bertrand/iter.h"


// avoid conflict with deprecated isascii() macro from <ctype.h> and assert() from
// <cassert>
#undef isascii
#undef assert


namespace bertrand {


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


template <meta::is<char> T>
static_str(T) -> static_str<1>;
template <auto S>
static_str(string_wrapper<S>) -> static_str<S.size()>;


/* ADL `swap()` method for `bertrand::static_str` instances. */
template <size_t N>
constexpr void swap(static_str<N>& a, static_str<N>& b)
    noexcept(noexcept(a.swap(b)))
{ a.swap(b); }


/* ADL `swap()` method for `bertrand::string_wrapper` instances. */
template <static_str self>
constexpr void swap(string_wrapper<self>& a, string_wrapper<self>& b)
    noexcept(noexcept(a.swap(b)))
{ a.swap(b); }


/* ADL `swap()` method for `bertrand::string_list` instances. */
template <static_str... Strings>
inline constexpr void swap(string_list<Strings...>& a, string_list<Strings...>& b)
    noexcept(noexcept(a.swap(b)))
{ a.swap(b); }


namespace impl {
    struct string_list_tag {};
    struct string_set_tag {};
    struct string_map_tag {};

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

        template <size_t I, bertrand::static_str... Strs>
        struct _unpack_string;
        template <size_t I, bertrand::static_str Str, bertrand::static_str... Strs>
        struct _unpack_string<I, Str, Strs...> {
            static constexpr bertrand::static_str value =
                _unpack_string<I - 1, Strs...>::value;
        };
        template <bertrand::static_str Str, bertrand::static_str... Strs>
        struct _unpack_string<0, Str, Strs...> {
            static constexpr bertrand::static_str value = Str;
        };

        template <bertrand::static_str...>
        constexpr bool strings_are_unique = true;
        template <bertrand::static_str First, bertrand::static_str... Rest>
        constexpr bool strings_are_unique<First, Rest...> =
            ((First != Rest) && ...) && strings_are_unique<Rest...>;

    }

    /// TODO: delete unpack_string and just use `meta::unpack_value` instead

    template <size_t I, bertrand::static_str... Strs>
    constexpr bertrand::static_str unpack_string =
        detail::_unpack_string<I, Strs...>::value;

    /// TODO: strings_are_unique should be in impl::, not meta::

    template <bertrand::static_str... Strings>
    concept strings_are_unique = detail::strings_are_unique<Strings...>;

}


/* A compile-time string literal type that can be used as a non-type template
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
struct static_str {
    using value_type = const char;
    using reference = value_type&;
    using const_reference = reference;
    using pointer = char*;
    using const_pointer = const char*;
    using size_type = size_t;
    using index_type = ssize_t;
    using difference_type = std::ptrdiff_t;
    using iterator = impl::contiguous_iterator<value_type>;
    using const_iterator = iterator;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

private:
    template <size_t M>
    friend struct bertrand::static_str;
    template <bertrand::static_str self>
    friend struct bertrand::string_wrapper;

public:
    char buffer[N + 1];  // +1 for null terminator

    constexpr static_str() = default;

    explicit constexpr static_str(char c) noexcept {
        std::fill_n(buffer, N, c);
        buffer[N] = '\0';
    }

    constexpr static_str(const char* arr) noexcept {
        std::copy_n(arr, N, buffer);
        buffer[N] = '\0';
    }

    /// TODO: from_int<> and from_float<> should probably be functions like everything
    /// else, and should account for arbitrary-precision integers and floats from
    /// bits.h.  That's going to be a challenge due to the circular dependency between
    /// them, though.

    /* Convert an integer into a string at compile time using the specified base. */
    template <long long num, size_type base = 10> requires (base >= 2 && base <= 36)
    static constexpr auto from_int = [] {
        return impl::int_to_static_string<num, base>;
    }();

    /* Convert a floating point number into a string at compile time with the
    specified precision. */
    template <double num, size_type precision = 6>
    static constexpr auto from_float = [] {
        return impl::float_to_static_string<num, precision>;
    }();

    /* Swap the contents of two static strings. */
    constexpr void swap(static_str& other) noexcept {
        if (this != &other) {
            std::swap_ranges(buffer, buffer + N + 1, other.buffer);
        }
    }

    /* Implicitly convert a static string to any other standard string type. */
    template <typename T>
        requires (!meta::std::format_string<T> && (
            meta::convertible_to<std::string_view, T> ||
            meta::convertible_to<std::string, T> ||
            meta::convertible_to<const char(&)[N + 1], T>
        ))
    [[nodiscard]] constexpr operator T() const noexcept(
        meta::nothrow::convertible_to<std::string_view, T> ||
        meta::nothrow::convertible_to<std::string, T> ||
        meta::nothrow::convertible_to<const char(&)[N + 1], T>
    ) {
        if constexpr (meta::convertible_to<std::string_view, T>) {
            return std::string_view{buffer, size()};
        } else if constexpr (meta::convertible_to<std::string, T>) {
            return std::string{buffer, size()};
        } else {
            return buffer;
        }
    }

    [[nodiscard]] constexpr pointer data() noexcept { return buffer; }
    [[nodiscard]] constexpr const_pointer data() const noexcept { return buffer; }
    [[nodiscard]] static constexpr size_type size() noexcept { return N; }
    [[nodiscard]] static constexpr index_type ssize() noexcept { return index_type(size()); }
    [[nodiscard]] static constexpr bool empty() noexcept { return !size(); }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return size(); }
    [[nodiscard]] constexpr iterator begin() const noexcept { return {buffer}; }
    [[nodiscard]] constexpr iterator cbegin() const noexcept { return {buffer}; }
    [[nodiscard]] constexpr iterator end() const noexcept { return {buffer + size()}; }
    [[nodiscard]] constexpr iterator cend() const noexcept { return {buffer + size()}; }
    [[nodiscard]] constexpr reverse_iterator rbegin() const noexcept {
        return reverse_iterator{end()};
    }
    [[nodiscard]] constexpr reverse_iterator crbegin() const noexcept {
        return reverse_iterator{cend()};
    }
    [[nodiscard]] constexpr reverse_iterator rend() const noexcept {
        return reverse_iterator{begin()};
    }
    [[nodiscard]] constexpr reverse_iterator crend() const noexcept {
        return reverse_iterator{cbegin()};
    }

    /* Get the character at index `I`, where `I` is known at compile time.  Applies
    Python-style wraparound for negative indices, and fails to compile if the index is
    out of bounds after normalization. */
    template <index_type I> requires (impl::valid_index<ssize(), I>)
    [[nodiscard]] constexpr char get() const noexcept {
        return buffer[impl::normalize_index<ssize(), I>()];
    }

    /* Get a slice from the string at compile time.  Takes an explicitly-initialized
    `bertrand::slice` pack describing the start, stop, and step indices.  Each index
    can be omitted by initializing it to `bertrand::None` (aka `std::nullopt`), which
    is equivalent to an empty slice index in Python.  Applies Python-style wraparound
    to both `start` and `stop`. */
    template <bertrand::slice s>
    [[nodiscard]] constexpr auto get() const noexcept {
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
    [[nodiscard]] constexpr reference operator[](index_type i) const noexcept(
        noexcept(data()[impl::normalize_index(ssize(), i)])
    ) {
        return data()[impl::normalize_index(ssize(), i)];
    }

    /* Slice operator.  Takes an explicitly-initialized `bertrand::slice` pack
    describing the start, stop, and step indices.  Each index can be omitted by
    initializing it to `bertrand::None` (aka `std::nullopt`), which is equivalent to an
    empty slice index in Python.  Applies Python-style wraparound to both `start` and
    `stop`. */
    template <meta::slice S>
    [[nodiscard]] constexpr auto operator[](S&& s) const
        noexcept (requires{{std::forward<S>(s).range(*this)} noexcept;})
    {
        return std::forward<S>(s).range(*this);
    }

    /* Get an iterator to the character at index `i`.  Applies Python-style wraparound
    for negative indices, and returns an `end()` iterator if the index is out of
    bounds after normalization. */
    [[nodiscard]] constexpr iterator at(index_type i) const noexcept {
        index_type index = i + ssize() * (i < 0);
        if (index < 0 || index >= ssize()) {
            return end();
        }
        return {data() + index};
    }

    /* Concatenate two static strings at compile time. */
    template <auto M>
    [[nodiscard]] constexpr auto operator+(const static_str<M>& other) const noexcept {
        static_str<N + M> result;
        std::copy_n(buffer, N, result.buffer);
        std::copy_n(other.buffer, other.size(), result.buffer + N);
        result.buffer[N + M] = '\0';
        return result;
    }

    /* Concatenate two static strings at compile time. */
    template <auto S>
    [[nodiscard]] constexpr auto operator+(string_wrapper<S>) const noexcept {
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
    /// is a compile-time constant, and can therefore be used to determine the size of
    /// the resulting string.  The `repeat<self, reps>()` method is a workaround that
    /// encodes the repetitions as a template parameter.  Perhaps this could be the
    /// subject of a future standards proposal, but for now, this is the best we can do.

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
    [[nodiscard]] constexpr auto operator<=>(const static_str<M>& other) const noexcept {
        return std::lexicographical_compare_three_way(
            buffer,
            buffer + N,
            other.buffer,
            other.buffer + M
        );
    }

    /* Lexicographically compare a static string to a string methods wrapper. */
    template <auto S>
    [[nodiscard]] constexpr auto operator<=>(string_wrapper<S>) const noexcept {
        return operator<=>(S);
    }

    /* Lexicographically compare a static string against a string literal. */
    [[nodiscard]] friend constexpr auto operator<=>(
        const static_str& self,
        const char* other
    ) noexcept {
        size_type i = 0;
        while (*other != '\0' && i < self.size()) {
            if (auto cmp = self.buffer[i] <=> *other; cmp != 0) {
                return cmp;
            }
            ++other;
            ++i;
        }
        return
            i < self.size() ?   std::strong_ordering::greater :
            *other != '\0'  ?   std::strong_ordering::less :
                                std::strong_ordering::equal;
    }

    /* Lexicographically compare a static string against a string literal. */
    [[nodiscard]] friend constexpr auto operator<=>(
        const char* other,
        const static_str& self
    ) noexcept {
        size_type i = 0;
        while (*other != '\0' && i < self.size()) {
            if (auto cmp = *other <= self.buffer[i]; cmp != 0) {
                return cmp;
            }
            ++other;
            ++i;
        }
        return
            *other != '\0'  ?   std::strong_ordering::greater :
            i < self.size() ?   std::strong_ordering::less :
                                std::strong_ordering::equal;
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
    [[nodiscard]] constexpr bool operator==(const static_str<M>& other) const noexcept {
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
    [[nodiscard]] constexpr bool operator==(string_wrapper<S>) const noexcept {
        return operator==(S);
    }

    /* Check for lexicographic equality between a static string and a string literal. */
    [[nodiscard]] friend constexpr bool operator==(
        const static_str& self,
        const char* other
    ) noexcept {
        size_type i = 0;
        while (*other != '\0' && i < self.size()) {
            if (*other != self.buffer[i]) {
                return false;
            }
            ++other;
            ++i;
        }
        return *other == '\0' && i == self.size();
    }

    /* Check for lexicographic equality between a static string and a string literal. */
    [[nodiscard]] friend constexpr bool operator==(
        const char* other,
        const static_str& self
    ) noexcept {
        size_type i = 0;
        while (*other != '\0' && i < self.size()) {
            if (*other != self.buffer[i]) {
                return false;
            }
            ++other;
            ++i;
        }
        return *other == '\0' && i == self.size();
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

    template <bertrand::static_str self, bertrand::static_str chars>
    static constexpr index_type first_non_stripped() noexcept {
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
    static constexpr index_type last_non_stripped() noexcept {
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

    /* Repeat a string a given number of times at compile time. */
    template <bertrand::static_str self, size_type reps>
    [[nodiscard]] static constexpr auto repeat() noexcept {
        static_str<self.size() * reps> result;
        for (size_type i = 0; i < reps; ++i) {
            std::copy_n(self.buffer, self.size(), result.buffer + (self.size() * i));
        }
        result.buffer[self.size() * reps] = '\0';
        return result;
    }

    /// NOTE: Because it is impossible to reference data from `this` within a
    /// constexpr context, all of these algorithms are implemented by taking the
    /// `self` string as a template parameter.  They are therefore private, so as not
    /// to confuse users who might expect them to be callable like Python methods.
    /// The `string_wrapper` class is used to abstract this distinction, yielding a
    /// naturally Pythonic interface.  If this restriction is lifted in a future
    /// version of the language, then these can be converted into normal instance
    /// methods, and the `string_wrapper` class can be removed.

    /* Equivalent to Python `str.capitalize()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static constexpr auto capitalize() noexcept {
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
    [[nodiscard]] static constexpr auto center() noexcept {
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
    [[nodiscard]] static constexpr size_type count() noexcept {
        index_type nstart = impl::truncate_index(self.size(), start).first;
        index_type nstop = impl::truncate_index(self.size(), stop).first;
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
    [[nodiscard]] static constexpr bool endswith() noexcept {
        return suffix.size() <= self.size() && std::equal(
            suffix.buffer,
            suffix.buffer + suffix.size(),
            self.buffer + self.size() - suffix.size()
        );
    }

    /* Equivalent to Python `str.expandtabs([tabsize])`. */
    template <bertrand::static_str self, size_type tabsize = 8>
    [[nodiscard]] static constexpr auto expandtabs() noexcept {
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

    /* Equivalent to Python `str.find(sub[, start[, stop]])`, except that it returns an
    iterator to the start of the substring, or an end iterator if the substring was not
    found. */
    template <
        bertrand::static_str self,
        bertrand::static_str sub,
        index_type start = 0,
        index_type stop = self.size()
    >
    [[nodiscard]] static constexpr auto find() noexcept {
        constexpr index_type nstart =
            impl::truncate_index(self.size(), start).first;
        constexpr index_type nstop =
            impl::truncate_index(self.size(), stop).first;
        for (index_type i = nstart; i < nstop; ++i) {
            if (std::equal(sub.buffer, sub.buffer + sub.size(), self.buffer + i)) {
                return self.begin() + i;
            }
        }
        return self.end();
    }

    /* Equivalent to Python `str.isalpha()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static constexpr bool isalpha() noexcept {
        for (size_type i = 0; i < self.size(); ++i) {
            if (!impl::char_isalpha(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isalnum()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static constexpr bool isalnum() noexcept {
        for (size_type i = 0; i < self.size(); ++i) {
            if (!impl::char_isalnum(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isascii()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static constexpr bool isascii() noexcept {
        for (size_type i = 0; i < self.size(); ++i) {
            if (!impl::char_isascii(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isdigit()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static constexpr bool isdigit() noexcept {
        for (size_type i = 0; i < self.size(); ++i) {
            if (!impl::char_isdigit(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.islower()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static constexpr bool islower() noexcept {
        for (size_type i = 0; i < self.size(); ++i) {
            if (!impl::char_islower(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.isspace()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static constexpr bool isspace() noexcept {
        for (size_type i = 0; i < self.size(); ++i) {
            if (!impl::char_isspace(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.istitle()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static constexpr bool istitle() noexcept {
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
    [[nodiscard]] static constexpr bool isupper() noexcept {
        for (size_type i = 0; i < self.size(); ++i) {
            if (!impl::char_isupper(self.buffer[i])) {
                return false;
            }
        }
        return self.size() > 0;
    }

    /* Equivalent to Python `str.join()`. */
    template <bertrand::static_str self>
    [[nodiscard]] static constexpr static_str<0> join() noexcept {
        return {};
    }

    /* Equivalent to Python `str.join(strings...)`. */
    template <
        bertrand::static_str self,
        bertrand::static_str first,
        bertrand::static_str... rest
    >
    [[nodiscard]] static constexpr auto join() noexcept {
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
    [[nodiscard]] static constexpr auto ljust() noexcept {
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
    [[nodiscard]] static constexpr auto lower() noexcept {
        static_str<self.size()> result;
        for (size_type i = 0; i < self.size(); ++i) {
            result.buffer[i] = impl::char_tolower(self.buffer[i]);
        }
        result.buffer[self.size()] = '\0';
        return result;
    }

    /* Equivalent to Python `str.lstrip([chars])`. */
    template <bertrand::static_str self, bertrand::static_str chars = " \t\n\r\f\v">
    [[nodiscard]] static constexpr auto lstrip() noexcept {
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
    [[nodiscard]] static constexpr auto partition() noexcept {
        constexpr auto it = find<self, sep>();
        if constexpr (it == self.end()) {
            return string_list<self, "", "">{};
        } else {
            constexpr size_type index = it - self.begin();
            constexpr size_type offset = index + sep.size();
            constexpr size_type remaining = self.size() - offset;
            return string_list<
                static_str<index>{self.buffer},
                sep,
                static_str<remaining>{self.buffer + offset}
            >{};
        }
    }

    /* Equivalent to Python `str.removeprefix()`, but evaluated statically at compile
    time. */
    template <bertrand::static_str self, bertrand::static_str prefix>
    [[nodiscard]] static constexpr auto removeprefix() noexcept {
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
    [[nodiscard]] static constexpr auto removesuffix() noexcept {
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

    /* Equivalent to Python `str.replace()`. */
    template <
        bertrand::static_str self,
        bertrand::static_str sub,
        bertrand::static_str repl,
        size_type max_count = std::numeric_limits<size_type>::max()
    >
    [[nodiscard]] static constexpr auto replace() noexcept {
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

    /* Equivalent to Python `str.rfind(sub[, start[, stop]])`, except that it returns
    an iterator to the start of the substring, or an end iterator if the substring was
    not found. */
    template <
        bertrand::static_str self,
        bertrand::static_str sub,
        index_type start = 0,
        index_type stop = self.size()
    >
    [[nodiscard]] static constexpr auto rfind() noexcept {
        constexpr index_type nstart =
            impl::truncate_index(self.size(), stop).first - 1;
        constexpr index_type nstop =
            impl::truncate_index(self.size(), start).first - 1;
        for (index_type i = nstart; i > nstop; --i) {
            if (std::equal(sub.buffer, sub.buffer + sub.size(), self.buffer + i)) {
                return self.begin() + i;
            }
        }
        return self.end();
    }

    /* Equivalent to Python `str.rjust(width[, fillchar])`. */
    template <bertrand::static_str self, size_type width, char fillchar = ' '>
    [[nodiscard]] static constexpr auto rjust() noexcept {
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
    [[nodiscard]] static constexpr auto rpartition() noexcept {
        constexpr auto it = rfind<self, sep>();
        if constexpr (it == self.end()) {
            return string_list<self, "", "">{};
        } else {
            constexpr size_type index = it - self.begin();
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
    [[nodiscard]] static constexpr auto rsplit() noexcept {
        constexpr size_type freq = count<self, sep>();
        if constexpr (freq == 0) {
            return string_list<self>{};
        } else {
            static constexpr size_type n = (freq < maxsplit ? freq : maxsplit) + 1;
            return []<size_type... Is>(std::index_sequence<Is...>) {
                static constexpr std::array<size_type, n> strides = [] {
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
                constexpr auto substr = []<size_type... Js>(std::index_sequence<Js...>) {
                    return self.buffer + self.size() - (
                        strides[0] + ... + (strides[Js + 1] + sep.size())
                    );
                };
                return string_list<static_str<strides[n - 1 - Is]>{
                    substr(std::make_index_sequence<n - 1 - Is>{})
                }...>{};
            }(std::make_index_sequence<n>{});
        }
    }

    /* Equivalent to Python `str.rstrip([chars])`. */
    template <bertrand::static_str self, bertrand::static_str chars = " \t\n\r\f\v">
    [[nodiscard]] static constexpr auto rstrip() noexcept {
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

    /* Return a new string containing a sorted permutation of these contents. */
    template <bertrand::static_str self, meta::default_constructible Less>
        requires (meta::iter_sortable<Less, char*, char*>)
    [[nodiscard]] static constexpr auto sort() noexcept {
        static_str<self.size()> result;
        std::copy_n(self.buffer, self.size(), result.buffer);
        result.buffer[self.size()] = '\0';
        bertrand::sort<Less>(result.data(), result.data() + self.size());
        return result;
    }

    /* Equivalent to Python `str.split(sep[, maxsplit])`.  The result is returned as a
    `string_list<...>` specialized to hold each substring. */
    template <
        bertrand::static_str self,
        bertrand::static_str sep,
        size_type maxsplit = std::numeric_limits<size_type>::max()
    > requires (sep.size() > 0)
    [[nodiscard]] static constexpr auto split() noexcept {
        constexpr size_type freq = count<self, sep>();
        if constexpr (freq == 0) {
            return string_list<self>{};
        } else {
            static constexpr size_type n = (freq < maxsplit ? freq : maxsplit) + 1;
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
    [[nodiscard]] static constexpr auto splitlines() noexcept {
        static constexpr size_type n = [] {
            size_type total = 1;
            for (size_type i = 0; i < self.size(); ++i) {
                char c = self.buffer[i];
                if (c == '\r') {
                    ++total;
                    i += self.buffer[i + 1] == '\n';  // skip newline character
                } else if (impl::char_islinebreak(c)) {
                    ++total;
                }
            }
            return total;
        }();
        if constexpr (n == 1) {
            return string_list<self>{};
        } else {
            static constexpr auto result = []<size_type... Is>(std::index_sequence<Is...>) {
                static constexpr std::array<size_type, n> strides = [] {
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
                constexpr auto substr = []<size_type... Js>(std::index_sequence<Js...>) {
                    if constexpr (keepends) {
                        return self.buffer + (0 + ... + strides[Js]);
                    } else {
                        size_type offset = 0;
                        ((offset += strides[Js] + 1 + (
                            self.buffer[offset] == '\r' && self.buffer[offset + 1] == '\n')
                        ), ...);
                        return self.buffer + offset;
                    }
                };
                return string_list<
                    static_str<strides[Is]>{substr(std::make_index_sequence<Is>{})}...
                >{};
            }(std::make_index_sequence<n>{});

            // strip empty line from the end
            if constexpr (result.template get<n - 1>().size() == 0) {
                return []<size_type... Is>(std::index_sequence<Is...>) {
                    return string_list<result.template get<Is>()...>{};
                }(std::make_index_sequence<n - 1>{});
            } else {
                return result;
            }
        }
    }

    /* Equivalent to Python `str.startswith(prefix)`, but evaluated statically at
    compile time. */
    template <bertrand::static_str self, bertrand::static_str prefix>
    [[nodiscard]] static constexpr bool startswith() noexcept {
        return (
            prefix.size() <= self.size() &&
            std::equal(prefix.buffer, prefix.buffer + prefix.size(), self.buffer)
        );
    }

    /* Equivalent to Python `str.strip([chars])`, but evaluated statically at compile
    time. */
    template <bertrand::static_str self, bertrand::static_str chars = " \t\n\r\f\v">
    [[nodiscard]] static constexpr auto strip() noexcept {
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
    [[nodiscard]] static constexpr auto swapcase() noexcept {
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
    [[nodiscard]] static constexpr auto title() noexcept {
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
    [[nodiscard]] static constexpr auto upper() noexcept {
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
    [[nodiscard]] static constexpr auto zfill() noexcept {
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
};


template <static_str self>
struct string_wrapper {
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

    /* Swap two static string wrappers.  Does nothing, since the strings are encoded
    entirely at compile time. */
    constexpr void swap(string_wrapper&) noexcept {}

    template <typename V> requires (meta::convertible_to<static_str<self.size()>, V>)
    [[nodiscard]] constexpr operator V() const
        noexcept(meta::nothrow::convertible_to<static_str<self.size()>, V>)
    { return self; }
    [[nodiscard]] static constexpr auto data() noexcept { return self.data(); }
    [[nodiscard]] static constexpr auto size() noexcept { return self.size(); }
    [[nodiscard]] static constexpr auto ssize() noexcept { return self.ssize(); }
    [[nodiscard]] static constexpr auto empty() noexcept { return self.empty(); }
    [[nodiscard]] explicit constexpr operator bool() noexcept { return self.operator bool(); }
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
    [[nodiscard]] static constexpr bool contains() noexcept {
        return find<sub, start, stop>() != self.end();
    }

    /* Get the character at index `I`, where `I` is known at compile time.  Applies
    Python-style wraparound for negative indices, and fails to compile if the index is
    out of bounds after normalization. */
    template <index_type I> requires (impl::valid_index<ssize(), I>)
    [[nodiscard]] static constexpr decltype(auto) get() noexcept {
        return self.template get<I>();
    }

    /* Get a slice from the string at compile time.  Takes an explicitly-initialized
    `bertrand::slice` pack describing the start, stop, and step indices.  Each index
    can be omitted by initializing it to `bertrand::None` (aka `std::nullopt`), which
    is equivalent to an empty slice index in Python.  Applies Python-style wraparound
    to both `start` and `stop`. */
    template <bertrand::slice s>
    [[nodiscard]] static constexpr auto get() noexcept {
        return string_wrapper<self.template get<s>()>{};
    }

    /* Access a character within the underlying buffer.  Applies Python-style
    wraparound for negative indices, and throws an `IndexError` if the index is out of
    bounds after normalization. */
    [[nodiscard]] static constexpr decltype(auto) operator[](index_type i) noexcept(
        noexcept(self[i])
    ) {
        return self[i];
    }

    /* Slice operator utilizing an initializer list.  Up to 3 (possibly negative)
    indices may be supplied according to Python semantics, with `bertrand::None` (aka
    `std::nullopt`) equating to an empty slice index in Python. */
    [[nodiscard]] static constexpr decltype(auto) operator[](bertrand::slice s) noexcept(
        noexcept(self[s])
    ) {
        return self[s];
    }

    /* Get an iterator to the character at index `i`.  Applies Python-style wraparound
    for negative indices, and returns an `end()` iterator if the index is out of
    bounds after normalization. */
    [[nodiscard]] constexpr auto at(index_type i) const noexcept(
        noexcept(self.at(i))
    ) {
        return self.at(i);
    }

    /* Concatenate two static strings at compile time. */
    template <auto S>
    [[nodiscard]] constexpr auto operator+(string_wrapper<S>) const noexcept {
        return string_wrapper<self + S>{};
    }

    /* Concatenate two static strings at compile time. */
    template <auto M>
    [[nodiscard]] constexpr auto operator+(const static_str<M>& other) const noexcept {
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
    [[nodiscard]] static constexpr auto repeat() noexcept {
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
    [[nodiscard]] constexpr auto operator<=>(string_wrapper<S>) const noexcept {
        return self <=> S;
    }

    /* Lexicographically compare two static strings at compile time. */
    template <auto M>
    [[nodiscard]] constexpr auto operator<=>(const static_str<M>& other) const noexcept {
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
    [[nodiscard]] constexpr auto operator==(string_wrapper<S>) const noexcept {
        return self == S;
    }

    /* Check for lexicographic equality between two static strings. */
    template <auto M>
    [[nodiscard]] constexpr auto operator==(const static_str<M>& other) const noexcept {
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
    [[nodiscard]] static constexpr auto capitalize() noexcept {
        return string_wrapper<self.template capitalize<self>()>{};
    }

    /* Equivalent to Python `str.center(width[, fillchar])`. */
    template <size_type width, char fillchar = ' '>
    [[nodiscard]] static constexpr auto center() noexcept {
        return string_wrapper<self.template center<self, width, fillchar>()>{};
    }

    /* Equivalent to Python `str.count(sub[, start[, stop]])`. */
    template <static_str sub, index_type start = 0, index_type stop = self.size()>
    [[nodiscard]] static constexpr auto count() noexcept {
        return self.template count<self, sub, start, stop>();
    }

    /* Equivalent to Python `str.endswith(suffix)`. */
    template <static_str suffix>
    [[nodiscard]] static constexpr auto endswith() noexcept {
        return self.template endswith<self, suffix>();
    }

    /* Equivalent to Python `str.expandtabs([tabsize])`. */
    template <size_type tabsize = 8>
    [[nodiscard]] static constexpr auto expandtabs() noexcept {
        return string_wrapper<self.template expandtabs<self, tabsize>()>{};
    }

    /* Equivalent to Python `str.find(sub[, start[, stop]])`, except that it returns an
    iterator to the start of the substring, or an end iterator if the substring was not
    found. */
    template <static_str sub, index_type start = 0, index_type stop = self.size()>
    [[nodiscard]] static constexpr auto find() noexcept {
        return self.template find<self, sub, start, stop>();
    }

    /* Equivalent to Python `str.index(sub[, start[, stop]])`.  Fails to compile if
    the substring is not present. */
    template <static_str sub, index_type start = 0, index_type stop = self.size()>
        requires (contains<sub, start, stop>())
    [[nodiscard]] static constexpr auto index() noexcept {
        return find<sub, start, stop>() - self.begin();
    }

    /* Equivalent to Python `str.isalpha()`. */
    [[nodiscard]] static constexpr auto isalpha() noexcept {
        return self.template isalpha<self>();
    }

    /* Equivalent to Python `str.isalnum()`. */
    [[nodiscard]] static constexpr auto isalnum() noexcept {
        return self.template isalnum<self>();
    }

    /* Equivalent to Python `str.isascii()`. */
    [[nodiscard]] static constexpr auto isascii() noexcept {
        return self.template isascii<self>();
    }

    /* Equivalent to Python `str.isdigit()`. */
    [[nodiscard]] static constexpr auto isdigit() noexcept {
        return self.template isdigit<self>();
    }

    /* Equivalent to Python `str.islower()`. */
    [[nodiscard]] static constexpr auto islower() noexcept {
        return self.template islower<self>();
    }

    /* Equivalent to Python `str.isspace()`. */
    [[nodiscard]] static constexpr auto isspace() noexcept {
        return self.template isspace<self>();
    }

    /* Equivalent to Python `str.istitle()`. */
    [[nodiscard]] static constexpr auto istitle() noexcept {
        return self.template istitle<self>();
    }

    /* Equivalent to Python `str.isupper()`. */
    [[nodiscard]] static constexpr auto isupper() noexcept {
        return self.template isupper<self>();
    }

    /* Equivalent to Python `str.join(strings...)`. */
    template <static_str... strs>
    [[nodiscard]] static constexpr auto join() noexcept {
        return string_wrapper<self.template join<self, strs...>()>{};
    }

    /* Equivalent to Python `str.ljust(width[, fillchar])`. */
    template <size_type width, char fillchar = ' '>
    [[nodiscard]] static constexpr auto ljust() noexcept {
        return string_wrapper<self.template ljust<self, width, fillchar>()>{};
    }

    /* Equivalent to Python `str.lower()`. */
    [[nodiscard]] static constexpr auto lower() noexcept {
        return string_wrapper<self.template lower<self>()>{};
    }

    /* Equivalent to Python `str.lstrip([chars])`. */
    template <static_str chars = " \t\n\r\f\v">
    [[nodiscard]] static constexpr auto lstrip() noexcept {
        return string_wrapper<self.template lstrip<self, chars>()>{};
    }

    /* Equivalent to Python `str.partition(sep)`. */
    template <static_str sep>
    [[nodiscard]] static constexpr auto partition() noexcept {
        return self.template partition<self, sep>();
    }

    /* Equivalent to Python `str.removeprefix()`, but evaluated statically at compile
    time. */
    template <static_str prefix>
    [[nodiscard]] static constexpr auto removeprefix() noexcept {
        return string_wrapper<self.template removeprefix<self, prefix>()>{};
    }

    /* Equivalent to Python `str.removesuffix()`, but evaluated statically at compile
    time. */
    template <static_str suffix>
    [[nodiscard]] static constexpr auto removesuffix() noexcept {
        return string_wrapper<self.template removesuffix<self, suffix>()>{};
    }

    /* Equivalent to Python `str.replace()`. */
    template <
        static_str sub,
        static_str repl,
        size_type max_count = std::numeric_limits<size_type>::max()
    >
    [[nodiscard]] static constexpr auto replace() noexcept {
        return string_wrapper<self.template replace<self, sub, repl, max_count>()>{};
    }

    /* Equivalent to Python `str.rfind(sub[, start[, stop]])`, except that it returns
    an iterator to the start of the substring, or an end iterator if the substring was
    not found. */
    template <static_str sub, index_type start = 0, index_type stop = self.size()>
    [[nodiscard]] static constexpr auto rfind() noexcept {
        return self.template rfind<self, sub, start, stop>();
    }

    /* Equivalent to Python `str.rindex(sub[, start[, stop]])`.  Fails to compile if
    the substring is not present. */
    template <static_str sub, index_type start = 0, index_type stop = self.size()>
        requires (contains<sub, start, stop>())
    [[nodiscard]] static constexpr auto rindex() noexcept {
        return rfind<sub, start, stop>() - self.begin();
    }

    /* Equivalent to Python `str.rjust(width[, fillchar])`. */
    template <size_type width, char fillchar = ' '>
    [[nodiscard]] static constexpr auto rjust() noexcept {
        return string_wrapper<self.template rjust<self, width, fillchar>()>{};
    }

    /* Equivalent to Python `str.rpartition(sep)`. */
    template <static_str sep>
    [[nodiscard]] static constexpr auto rpartition() noexcept {
        return self.template rpartition<self, sep>();
    }

    /* Equivalent to Python `str.rsplit(sep[, maxsplit])`. */
    template <
        static_str sep,
        size_type maxsplit = std::numeric_limits<size_type>::max()
    > requires (sep.size() > 0)
    [[nodiscard]] static constexpr auto rsplit() noexcept {
        return self.template rsplit<self, sep, maxsplit>();
    }

    /* Equivalent to Python `str.rstrip([chars])`. */
    template <static_str chars = " \t\n\r\f\v">
    [[nodiscard]] static constexpr auto rstrip() noexcept {
        return string_wrapper<self.template rstrip<self, chars>()>{};
    }

    /* Return a new string containing a sorted permutation of these contents. */
    template <meta::default_constructible Less = impl::Less>
        requires (meta::iter_sortable<Less, char*, char*>)
    [[nodiscard]] static constexpr auto sort() noexcept {
        return string_wrapper<self.template sort<self, Less>()>{};
    }

    /* Equivalent to Python `str.split(sep[, maxsplit])`.  The result is returned as a
    `string_list<...>` specialized to hold each substring. */
    template <
        static_str sep,
        size_type maxsplit = std::numeric_limits<size_type>::max()
    > requires (sep.size() > 0)
    [[nodiscard]] static constexpr auto split() noexcept {
        return self.template split<self, sep, maxsplit>();
    }

    /* Equivalent to Python `str.splitlines([keepends])`. */
    template <bool keepends = false>
    [[nodiscard]] static constexpr auto splitlines() noexcept {
        return self.template splitlines<self, keepends>();
    }

    /* Equivalent to Python `str.startswith(prefix)`, but evaluated statically at
    compile time. */
    template <static_str prefix>
    [[nodiscard]] static constexpr auto startswith() noexcept {
        return self.template startswith<self, prefix>();
    }

    /* Equivalent to Python `str.strip([chars])`, but evaluated statically at compile
    time. */
    template <static_str chars = " \t\n\r\f\v">
    [[nodiscard]] static constexpr auto strip() noexcept {
        return string_wrapper<self.template strip<self, chars>()>{};
    }

    /* Equivalent to Python `str.swapcase()`, but evaluated statically at compile
    time. */
    [[nodiscard]] static constexpr auto swapcase() noexcept {
        return string_wrapper<self.template swapcase<self>()>{};
    }

    /* Equivalent to Python `str.title()`, but evaluated statically at compile time. */
    [[nodiscard]] static constexpr auto title() noexcept {
        return string_wrapper<self.template title<self>()>{};
    }

    /* Equivalent to Python `str.upper()`, but evaluated statically at compile time. */
    [[nodiscard]] static constexpr auto upper() noexcept {
        return string_wrapper<self.template upper<self>()>{};
    }

    /* Equivalent to Python `str.zfill(width)`, but evaluated statically at compile
    time. */
    template <size_type width>
    [[nodiscard]] static constexpr auto zfill() noexcept {
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
        static constexpr std::pair<size_t, size_t> min_max =
            std::minmax({Keys.size()...});
        template <size_t I> requires (I == 0)
        static constexpr std::pair<size_t, size_t> min_max<I> = {0, 0};

    public:
        static constexpr size_t min_length = min_max<table_size>.first;
        static constexpr size_t max_length = min_max<table_size>.second;

    private:
        /* Count the occurrences of a particular character at a given offset across all
        strings. */
        template <static_str... Strs> requires (sizeof...(Strs) == 0)
        static constexpr size_t counts(unsigned char, size_t) { return 0; }
        template <static_str First, static_str... Rest>
        static constexpr size_t counts(unsigned char c, size_t pos) {
            return
                counts<Rest...>(c, pos) +
                (pos < First.size() && First.data()[pos] == c);
        }

        /* Find the index of the first string at which a particular character occurs
        for a given offset. */
        template <size_t, unsigned char, static_str...>
        static constexpr size_t first_occurrence = 0;
        template <size_t I, unsigned char C, static_str S, static_str... R> requires (I < S.size())
        static constexpr size_t first_occurrence<I, C, S, R...> =
            S.data()[I] == C ? 0 : first_occurrence<I, C, R...> + 1;
        template <size_t I, unsigned char C, static_str S, static_str... R> requires (I >= S.size())
        static constexpr size_t first_occurrence<I, C, S, R...> = first_occurrence<I, C, R...> + 1;

        /* Count the number of unique characters that occur at a given offset across
        all strings. */
        template <size_t, size_t, static_str...>
        static constexpr size_t _variation = 0;
        template <size_t I, size_t J, static_str S, static_str... R> requires (I < S.size())
        static constexpr size_t _variation<I, J, S, R...> =
            _variation<I, J + 1, R...> + (J == first_occurrence<I, S.data()[I], Keys...>);
        template <size_t I, size_t J, static_str S, static_str... R> requires (I >= S.size())
        static constexpr size_t _variation<I, J, S, R...> = _variation<I, J + 1, R...>;
        template <size_t I, static_str... Strings>
        static constexpr size_t variation = _variation<I, 0, Strings...>;

        /* An array holding the count of unique characters across each index of the
        input strings, up to `max_length`. */
        static constexpr std::array<size_t, max_length> frequencies =
            []<size_t... Is>(std::index_sequence<Is...>) {
                return std::array<size_t, max_length>{variation<Is, Keys...>...};
            }(std::make_index_sequence<max_length>{});

        /* An array of indices into the frequencies table sorted in descending order,
        with the highest variation indices coming first. */
        static constexpr std::array<size_t, max_length> sorted_freq_indices =
            []<size_t... Is>(std::index_sequence<Is...>) {
                std::array<size_t, max_length> result {Is...};
                std::sort(result.begin(), result.end(), [](size_t a, size_t b) {
                    return frequencies[a] > frequencies[b];
                });
                return result;
            }(std::make_index_sequence<max_length>{});

        using Weights = std::array<unsigned char, 256>;
        using collision = std::pair<std::string_view, std::string_view>;

        /* Check to see if a set of candidate weights produce any collisions for a
        given number of significant characters. */
        template <static_str...>
        struct collisions {
            static constexpr collision operator()(const Weights&, size_t) {
                return {"", ""};  // no collisions across all strings
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
                    return {"", ""};  // no collisions for this string
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
                        unsigned char c = pos < F.size() ? F.data()[pos] : 0;
                        hash += weights[c];
                    }
                    if ((hash % table_size) == idx) {
                        return {orig, {F.buffer, F.size()}};  // collision at this string
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
                    return result;  // collision at this string
                }
                // continue to next string
                return collisions<Rest...>{}(weights, significant_chars);
            }
        };

        /* Find an associative value array that produces perfect hashes over the input
        keys. */
        static constexpr auto find_hash = [] -> std::tuple<size_t, Weights, bool> {
            Weights weights;

            // start with zero significant characters and increase until a valid
            // set of weights is found
            for (size_t i = 0; i <= max_length; ++i) {
                weights.fill(1);  // initialize to uniform weights

                // search is limited by template recursion depth
                for (size_t j = 0; j < TEMPLATE_RECURSION_LIMIT; ++j) {
                    collision result = collisions<Keys...>{}(weights, i);
                    if (result.first == result.second) {
                        return {i, weights, true};  // found valid weights
                    }

                    // adjust weights for next iteration
                    bool identical = true;
                    for (size_t k = 0; k < i; ++k) {
                        size_t pos = sorted_freq_indices[k];
                        unsigned char c1 = pos < result.first.size() ?
                            result.first[pos] : 0;
                        unsigned char c2 = pos < result.second.size() ?
                            result.second[pos] : 0;
                        if (c1 != c2) {
                            // increment weight of the less frequent character, which
                            // is the most discriminatory
                            if (counts<Keys...>(c1, pos) < counts<Keys...>(c2, pos)) {
                                ++weights[c1];
                            } else {
                                ++weights[c2];
                            }
                            identical = false;
                            break;
                        }
                    }
                    // if all significant characters are the same, widen the search
                    // to consider additional characters
                    if (identical) {
                        break;
                    }
                }
            } 

            // no valid weights were found => hash is invalid
            return {0, weights, false};
        }();

    public:
        /* The number of significant characters needed for the perfect hash
        function. */
        static constexpr size_t significant_chars = std::get<0>(find_hash);

        /* The array of weights for every valid character. */
        static constexpr Weights weights = std::get<1>(find_hash);

        /* True if a perfect hash function was found.  False otherwise. */
        static constexpr bool exists = std::get<2>(find_hash);

        /* An array holding the positions of the significant characters for the
        associative value array, in proper traversal order. */
        static constexpr std::array<size_t, significant_chars> positions =
            []<size_t... Is>(std::index_sequence<Is...>) {
                std::array<size_t, significant_chars> positions {
                    sorted_freq_indices[Is]...
                };
                std::sort(positions.begin(), positions.end());
                return positions;
            }(std::make_index_sequence<significant_chars>{});

        /* A template constraint that checks whether a type represents a valid input to
        the call operator. */
        template <typename T>
        static constexpr bool hashable =
            meta::convertible_to<T, const char*> ||
            meta::convertible_to<T, std::string_view> ||
            meta::convertible_to<T, std::string>;

        /* Hash a string according to the computed perfect hash function. */
        template <meta::static_str Key>
        [[nodiscard]] static constexpr size_t operator()(const Key& str) noexcept {
            constexpr size_t len = Key::size();
            size_t out = 0;
            for (size_t pos : positions) {
                out += weights[pos < len ? str.data()[pos] : 0];
            }
            return out;
        }

        /* Hash a string according to the computed perfect hash function. */
        template <size_t N>
        [[nodiscard]] static constexpr size_t operator()(const char(&str)[N]) noexcept {
            constexpr size_t M = N - 1;
            size_t out = 0;
            for (size_t pos : positions) {
                out += weights[pos < M ? str[pos] : 0];
            }
            return out;
        }

        /* Hash a string according to the computed perfect hash function and record its
        length as an out parameter. */
        template <size_t N>
        [[nodiscard]] static constexpr size_t operator()(
            const char(&str)[N],
            size_t& len
        ) noexcept {
            constexpr size_t M = N - 1;
            size_t out = 0;
            for (size_t pos : positions) {
                out += weights[pos < M ? str[pos] : 0];
            }
            len = M;
            return out;
        }

        /* Hash a string according to the computed perfect hash function. */
        template <meta::convertible_to<const char*> T>
            requires (!meta::static_str<T> && !meta::string_literal<T>)
        [[nodiscard]] static constexpr size_t operator()(T&& str) noexcept(
            noexcept(static_cast<const char*>(std::forward<T>(str)))
        ) {
            const char* start = std::forward<T>(str);
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

        /* Hash a string according to the computed perfect hash algorithm and record
        its length as an out parameter. */
        template <meta::convertible_to<const char*> T>
            requires (!meta::static_str<T> && !meta::string_literal<T>)
        [[nodiscard]] static constexpr size_t operator()(T&& str, size_t& len) noexcept(
            noexcept(static_cast<const char*>(std::forward<T>(str)))
        ) {
            const char* start = std::forward<T>(str);
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

        /* Hash a string according to the computed perfect hash function. */
        template <meta::convertible_to<std::string_view> T>
            requires (
                !meta::static_str<T> &&
                !meta::string_literal<T> &&
                !meta::convertible_to<T, const char*>
            )
        [[nodiscard]] static constexpr size_t operator()(T&& str) noexcept(
            noexcept(std::string_view(std::forward<T>(str)))
        ) {
            std::string_view s = std::forward<T>(str);
            size_t out = 0;
            for (size_t pos : positions) {
                out += weights[pos < s.size() ? s[pos] : 0];
            }
            return out;
        }

        /* Hash a string according to the computed perfect hash function. */
        template <meta::convertible_to<std::string> T>
            requires (
                !meta::static_str<T> &&
                !meta::string_literal<T> &&
                !meta::convertible_to<T, const char*> &&
                !meta::convertible_to<T, std::string_view>
            )
        [[nodiscard]] static constexpr size_t operator()(T&& str) noexcept(
            noexcept(std::string(std::forward<T>(str)))
        ) {
            std::string s = std::forward<T>(str);
            size_t out = 0;
            for (size_t pos : positions) {
                out += weights[pos < s.size() ? s[pos] : 0];
            }
            return out;
        }
    };

    template <meta::not_void T> requires (!meta::reference<T>)
    struct hashed_string_iterator {
        using iterator_category = std::random_access_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using reference = value_type&;
        using pointer = value_type*;

    private:
        pointer m_table = nullptr;
        const size_t* m_hash_index = nullptr;
        difference_type m_idx = 0;
        difference_type m_length = 0;

    public:
        constexpr hashed_string_iterator() noexcept = default;
        constexpr hashed_string_iterator(
            pointer table,
            const size_t* hash_index,
            difference_type index,
            difference_type length
        ) noexcept :
            m_table(table),
            m_hash_index(hash_index),
            m_idx(index),
            m_length(length)
        {}

        [[nodiscard]] constexpr reference operator*() const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (m_idx < 0 || m_idx >= m_length) {
                    throw IndexError(std::to_string(m_idx));
                }
            }
            return m_table[m_hash_index[m_idx]];
        }

        [[nodiscard]] constexpr pointer operator->() const noexcept(!DEBUG) {
            return &**this;
        }

        [[nodiscard]] constexpr reference operator[](difference_type n) const
            noexcept(!DEBUG)
        {
            difference_type index = m_idx + n;
            if constexpr (DEBUG) {
                if (index < 0 || index >= m_length) {
                    throw IndexError(std::to_string(index));
                }
            }
            return m_table[m_hash_index[index]];
        }

        constexpr hashed_string_iterator& operator++() noexcept {
            ++m_idx;
            return *this;
        }

        [[nodiscard]] constexpr hashed_string_iterator operator++(int) noexcept {
            hashed_string_iterator copy = *this;
            ++m_idx;
            return copy;
        }

        constexpr hashed_string_iterator& operator+=(difference_type n) noexcept {
            m_idx += n;
            return *this;
        }

        [[nodiscard]] constexpr hashed_string_iterator operator+(
            difference_type n
        ) const noexcept {
            return {m_table, m_hash_index, m_idx + n, m_length};
        }

        constexpr hashed_string_iterator& operator--() noexcept {
            --m_idx;
            return *this;
        }

        [[nodiscard]] constexpr hashed_string_iterator operator--(int) noexcept {
            hashed_string_iterator copy = *this;
            --m_idx;
            return copy;
        }

        constexpr hashed_string_iterator& operator-=(difference_type n) noexcept {
            m_idx -= n;
            return *this;
        }

        [[nodiscard]] constexpr hashed_string_iterator operator-(
            difference_type n
        ) const noexcept {
            return {m_table, m_hash_index, m_idx - n, m_length};
        }

        [[nodiscard]] constexpr difference_type operator-(
            const hashed_string_iterator& other
        ) const noexcept {
            return m_idx - other.m_idx;
        }

        [[nodiscard]] constexpr auto operator<=>(
            const hashed_string_iterator& other
        ) const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (m_table != other.m_table) {
                    throw ValueError(
                        "cannot compare iterators from different containers"
                    );
                }
            }
            return m_idx <=> other.m_idx;
        }

        [[nodiscard]] constexpr bool operator==(
            const hashed_string_iterator& other
        ) const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (m_table != other.m_table) {
                    throw ValueError(
                        "cannot compare iterators from different containers"
                    );
                }
            }
            return m_idx == other.m_idx;
        }
    };

}


namespace meta {

    template <typename T>
    concept string_list = inherits<T, impl::string_list_tag>;

    template <typename T>
    concept string_set = inherits<T, impl::string_set_tag>;

    template <typename T>
    concept string_map = inherits<T, impl::string_map_tag>;

    template <bertrand::static_str... Keys>
    concept perfectly_hashable =
        strings_are_unique<Keys...> && impl::minimal_perfect_hash<Keys...>::exists;

}


/* A compile-time perfect hash set with a finite set of compile-time strings as keys.

This data structure will attempt to compute a minimal perfect hash function over the
input strings at compile time, granting optimal O(1) lookups at both compile time and
runtime without any collisions.  The computed hash function is based on the algorithm
used by tools like `gperf`, which only need to consider the N most variable characters
in the input strings, where N is minimized using an associative array of relative
weights for each character.  Searches therefore devolve to just a handful of character
lookups at fixed offsets within the string, followed by an equality comparison to
validate the result, which approaches the maximum possible theoretical performance for
a hash table in any language.  In fact, for search strings that are known at compile
time, the lookup logic can be completely optimized out, skipping straight to the
final value without any intermediate computation. */
template <static_str... Keys> requires (meta::perfectly_hashable<Keys...>)
struct string_set;


/* A compile-time perfect hash map with a finite set of compile-time strings as keys.

This data structure will attempt to compute a minimal perfect hash function over the
input strings at compile time, granting optimal O(1) lookups at both compile time and
runtime without any collisions.  The computed hash function is based on the algorithm
used by tools like `gperf`, which only need to consider the N most variable characters
in the input strings, where N is minimized using an associative array of relative
weights for each character.  Searches therefore devolve to just a handful of character
lookups at fixed offsets within the string, followed by an equality comparison to
validate the result, which approaches the maximum possible theoretical performance for
a hash table in any language.  In fact, for search strings that are known at compile
time, the lookup logic can be completely optimized out, skipping straight to the
final value without any intermediate computation. */
template <meta::not_void T, static_str... Keys> requires (meta::perfectly_hashable<Keys...>)
struct string_map;


/* ADL `swap()` method for `bertrand::string_set` instances. */
template <static_str... Keys>
constexpr void swap(string_set<Keys...>& a, string_set<Keys...>& b)
    noexcept(noexcept(a.swap(b)))
{ a.swap(b); }


/* ADL `swap()` method for `bertrand::string_map` instances. */
template <typename T, static_str... Keys>
constexpr void swap(string_map<T, Keys...>& a, string_map<T, Keys...>& b)
    noexcept(noexcept(a.swap(b)))
    requires(requires{a.swap(b);})
{ a.swap(b); }


template <static_str... Strings>
struct string_list : impl::string_list_tag {
    using value_type = const std::string_view;
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
    using const_reverse_iterator = reverse_iterator;
    using slice = impl::slice<iterator>;
    using const_slice = impl::slice<const_iterator>;

    /* Swap the contents of two string lists.  Does nothing, since the strings are
    encoded entirely at compile time. */
    constexpr void swap(string_list&) noexcept {}

    [[nodiscard]] static constexpr pointer data() noexcept { return m_data.data(); }
    [[nodiscard]] static constexpr size_type size() noexcept { return sizeof...(Strings); }
    [[nodiscard]] static constexpr index_type ssize() noexcept { return index_type(size()); }
    [[nodiscard]] static constexpr bool empty() noexcept { return size() == 0; }
    [[nodiscard]] explicit constexpr operator bool() noexcept { return !empty(); }
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

    /* Check whether the list contains an arbitrary string. */
    template <static_str Key>
    [[nodiscard]] static constexpr bool contains() noexcept {
        return ((Key == Strings) || ...);
    }

    /* Check whether the list contains an arbitrary string. */
    template <typename T>
    [[nodiscard]] static constexpr bool contains(T&& key)
        noexcept((meta::nothrow::has_eq<T, decltype(Strings)> && ...))
        requires((meta::has_eq<T, decltype(Strings)> && ...))
    {
        return ((key == Strings) || ...);
    }

    /* Count the number of occurrences of a particular string within the list. */
    template <static_str Key>
    [[nodiscard]] static constexpr size_type count() noexcept {
        return ((size_type(0) + ... + (Key == Strings)));
    }

    /* Count the number of occurrences of a particular string within the list. */
    template <typename T>
    [[nodiscard]] static constexpr size_type count(const T& key)
        noexcept((meta::nothrow::has_eq<T, decltype(Strings)> && ...))
        requires((meta::has_eq<T, decltype(Strings)> && ...))
    {
        return ((size_type(0) + ... + (key == Strings)));
    }

    /* Get the index of the first occurrence of a string within the list.  Fails to
    compile if the string is not present. */
    template <static_str Key> requires (contains<Key>())
    [[nodiscard]] static constexpr index_type index() noexcept {
        return []<index_type I = 0>(this auto&& self) {
            if constexpr (Key == meta::unpack_string<I, Strings...>) {
                return I;
            } else {
                return std::forward<decltype(self)>(self).template operator()<I + 1>();
            }
        }();
    }

    /* Get the index of the first occurrence of a string within the list.  Returns an
    empty optional if the string is not present in the list. */
    template <typename T>
    [[nodiscard]] static constexpr Optional<index_type> index(const T& key)
        noexcept((meta::nothrow::has_eq<T, decltype(Strings)> && ...))
        requires((meta::has_eq<T, decltype(Strings)> && ...))
    {
        return []<index_type I = 0>(this auto&& self, const T& key) -> Optional<index_type> {
            if constexpr (I < ssize()) {
                if (key == meta::unpack_string<I, Strings...>) {
                    return I;
                }
                return std::forward<decltype(self)>(self).template operator()<I + 1>(key);
            } else {
                return None;
            }
        }(key);
    }

    /* Get an iterator to the first occurrence of a string within the list.  Returns
    an `end()` iterator if the string is not present. */
    template <static_str Key> requires (contains<Key>())
    [[nodiscard]] static constexpr iterator find() noexcept {
        return at<index<Key>()>();
    }

    /* Get an iterator to the first occurrence of a string within the list.  Returns
    an `end()` iterator if the string is not present. */
    template <typename T>
    [[nodiscard]] static constexpr iterator find(T&& key)
        noexcept(noexcept(index(std::forward<T>(key))))
        requires(requires{index(std::forward<T>(key));})
    {
        auto idx = index(std::forward<T>(key));
        return idx.has_value() ? at(idx.value()) : end();
    }

    /* Get the string at index I, where `I` is known at compile time.  Applies
    Python-style wraparound for negative indices, and fails to compile if the index
    is out of bounds after normalization. */
    template <index_type I> requires (impl::valid_index<ssize(), I>)
    [[nodiscard]] static constexpr const auto& get() noexcept {
        constexpr size_type idx = size_type(impl::normalize_index<ssize(), I>());
        return meta::unpack_string<idx, Strings...>;
    }

    /* Get a slice from the list at compile time.  Takes an explicitly-initialized
    `bertrand::slice` pack describing the start, stop, and step indices.  Each index
    can be omitted by initializing it to `bertrand::None` (aka `std::nullopt`), which
    is equivalent to an empty slice index in Python.  Applies Python-style wraparound
    to both `start` and `stop`. */
    template <bertrand::slice s>
    [[nodiscard]] static constexpr auto get() noexcept {
        static constexpr auto indices = s.normalize(ssize());
        return []<size_t... Is>(std::index_sequence<Is...>) {
            return string_list<
                meta::unpack_string<indices.start + Is * indices.step, Strings...>...
            >{};
        }(std::make_index_sequence<indices.length>{});
    }

    /* Get the string at index i, where `i` is known at runtime.  Applies Python-style
    wraparound for negative indices, and throws an `IndexError` if the index is out of
    bounds after normalization. */
    [[nodiscard]] static constexpr reference operator[](index_type i) noexcept(
        noexcept(data()[impl::normalize_index(ssize(), i)])
    ) {
        return data()[impl::normalize_index(ssize(), i)];
    }

    /* Slice operator.  Takes an explicitly-initialized `bertrand::slice` pack
    describing the start, stop, and step indices, and returns a slice object containing
    the strings within the slice.  Each index can be omitted by initializing it to
    `bertrand::None` (aka `std::nullopt`), which is equivalent to an empty slice index
    in Python.  Applies Python-style wraparound to both `start` and `stop`. */
    [[nodiscard]] constexpr slice operator[](bertrand::slice s) const
        noexcept(noexcept(slice{*this, s.normalize(ssize())}))
    {
        return {*this, s.normalize(ssize())};
    }

    /* Get an iterator to the string at index `i`.  Applies Python-style wraparound for
    negative indices, and returns an `end()` iterator if the index is out of bounds
    after normalization. */
    [[nodiscard]] static constexpr iterator at(index_type i) noexcept {
        index_type index = i + ssize() * (i < 0);
        if (index < 0 || index >= ssize()) {
            return end();
        }
        return {data() + index};
    }

private:

    template <auto indices, index_type I, static_str... Strs>
    struct forward_remove {
        using type = forward_remove<
            indices,
            I + 1,
            Strs...,
            meta::unpack_string<I, Strings...>
        >::type;
    };
    template <auto indices, index_type I, static_str... Strs>
        requires (I >= indices.start && I < indices.stop)
    struct forward_remove<indices, I, Strs...> {
        template <index_type J>
        struct filter {
            using type = forward_remove<
                indices,
                I + 1,
                Strs...,
                meta::unpack_string<J, Strings...>
            >::type;
        };
        template <index_type J> requires ((J - indices.start) % indices.step == 0)
        struct filter<J> { using type = forward_remove<indices, I + 1, Strs...>::type; };
        using type = filter<I>::type;
    };
    template <auto indices, index_type I, static_str... Strs> requires (I == ssize())
    struct forward_remove<indices, I, Strs...> { using type = string_list<Strs...>; };

    template <auto indices, index_type I, static_str... Strs>
    struct backward_remove {
        using type = backward_remove<
            indices,
            I - 1,
            meta::unpack_string<I, Strings...>,
            Strs...
        >::type;
    };
    template <auto indices, index_type I, static_str... Strs>
        requires (I <= indices.start && I > indices.stop)
    struct backward_remove<indices, I, Strs...> {
        template <index_type J>
        struct filter {
            using type = backward_remove<
                indices,
                I - 1,
                meta::unpack_string<J, Strings...>,
                Strs...
            >::type;
        };
        template <index_type J> requires ((indices.start - J) % indices.step == 0)
        struct filter<J> { using type = backward_remove<indices, I - 1, Strs...>::type; };
        using type = filter<I>::type;
    };
    template <auto indices, index_type I, static_str... Strs> requires (I == -1)
    struct backward_remove<indices, I, Strs...> { using type = string_list<Strs...>; };

    template <typename Less>
    struct sort_helper {
        using type = std::pair<std::string_view, size_t>;
        static constexpr auto array = []<size_t... Is>(std::index_sequence<Is...>) {
            std::array<type, size()> out {type{Strings, Is}...};
            bertrand::sort<sort_helper>(out);
            return out;
        }(std::make_index_sequence<size()>{});
        template <size_t I, typename out>
        struct call { using type = out; };
        template <size_t I, static_str... Strs> requires (I < size())
        struct call<I, string_list<Strs...>> {
            using type = call<I + 1, string_list<
                Strs...,
                meta::unpack_string<array[I].second, Strings...>
            >>::type;
        };
        Less less;
        constexpr bool operator()(const type& a, const type& b) const noexcept {
            return less(a.first, b.first);
        }
    };

public:

    /* Return a new string list containing a sorted permutation of the current list,
    according to a less-than comparison function type accepting two
    `std::string_view`s. */
    template <meta::default_constructible Less = impl::Less>
        requires (meta::iter_sortable<Less, std::string_view*, std::string_view*>)
    [[nodiscard]] static constexpr auto sort() noexcept {
        return typename sort_helper<Less>::template call<0, string_list<>>::type{};
    }

    /* Remove the first occurrence of a string from the list, returning a new list
    without that element.  Fails to compile if the string is not present. */
    template <static_str Key> requires (contains<Key>())
    [[nodiscard]] static constexpr auto remove() noexcept {
        constexpr size_type idx = size_type(index<Key>());
        return []<size_t... Prev, size_t... Next>(
            std::index_sequence<Prev...>,
            std::index_sequence<Next...>
        ) {
            return string_list<
                meta::unpack_string<Prev, Strings...>...,
                meta::unpack_string<idx + 1 + Next, Strings...>...
            >{};
        }(
            std::make_index_sequence<idx>{},
            std::make_index_sequence<size() - idx - 1>{}
        );
    }

    /* Remove the string at index I, returning a new list without that element.
    Applies Python-style wraparound for negative indices, and fails to compile if the
    index is out of bounds after normalization. */
    template <index_type I> requires (impl::valid_index<ssize(), I>)
    [[nodiscard]] static constexpr auto remove() noexcept {
        constexpr size_type idx = size_type(impl::normalize_index<ssize(), I>());
        return []<size_t... Prev, size_t... Next>(
            std::index_sequence<Prev...>,
            std::index_sequence<Next...>
        ) {
            return string_list<
                meta::unpack_string<Prev, Strings...>...,
                meta::unpack_string<idx + 1 + Next, Strings...>...
            >{};
        }(
            std::make_index_sequence<idx>{},
            std::make_index_sequence<size() - idx - 1>{}
        );
    }

    /* Remove a slice from the list, returning a new list without the sliced elements.
    Takes an explicitly-initialized `bertrand::slice` pack describing the start, stop,
    and step indices.  Each index can be omitted by initializing it to `bertrand::None`
    (aka `std::nullopt`), which is equivalent to an empty slice index in Python.
    Applies Python-style wraparound to both `start` and `stop`. */
    template <bertrand::slice s>
    [[nodiscard]] static constexpr auto remove() noexcept {
        static constexpr auto indices = s.normalize(ssize());
        if constexpr (indices.length == 0) {
            return string_list{};
        } else if constexpr (indices.step > 0) {
            return typename forward_remove<indices, 0>::type{};
        } else {
            return typename backward_remove<indices, ssize() - 1>::type{};
        }
    }

    /* Equivalent to Python `sep.join(strings...)`. */
    template <static_str sep>
    [[nodiscard]] static constexpr auto join() noexcept {
        return string_wrapper<sep>::template join<Strings...>();
    }

    /* True if the list contains only unique strings.  False otherwise. */
    static constexpr bool unique = meta::strings_are_unique<Strings...>;

private:

    template <index_type I, static_str... Strs>
    struct unique_strings { using type = unique_strings<I + 1, Strs...> ::type; };
    template <index_type I, static_str... Strs>
        requires (I < ssize() && I == index<meta::unpack_string<I, Strings...>>())
    struct unique_strings<I, Strs...> {
        using type = unique_strings<
            I + 1,
            Strs...,
            meta::unpack_string<I, Strings...>
        >::type;
    };
    template <index_type I, static_str... Strs> requires (I == ssize())
    struct unique_strings<I, Strs...> { using type = string_list<Strs...>; };

public:

    /* Filter out any duplicate strings in the list. */
    [[nodiscard]] static constexpr auto to_unique() noexcept(
        noexcept(typename unique_strings<0>::type{})
    ) {
        return typename unique_strings<0>::type{};
    }

    /* Convert this string list into a string set, assuming the strings it contains are
    perfectly hashable. */
    [[nodiscard]] static constexpr auto to_set()
        noexcept(noexcept(string_set<Strings...>{}))
        requires(meta::perfectly_hashable<Strings...>)
    {
        return string_set<Strings...>{};
    }

    /* Convert this string list into a string map with the given values, assuming the
    strings it contains are perfectly hashable. */
    template <typename... Args>
        requires (
            sizeof...(Args) == size() &&
            meta::has_common_type<Args...> &&
            meta::perfectly_hashable<Strings...>
        )
    [[nodiscard]] static constexpr auto to_map(Args&&... args) noexcept(
        noexcept(string_map<
            meta::remove_reference<meta::common_type<Args...>>,
            Strings...
        >{std::forward<Args>(args)...})
    ) {
        return string_map<
            meta::remove_reference<meta::common_type<Args...>>,
            Strings...
        >{std::forward<Args>(args)...};
    }

    /* Repeat the contents of the list `reps` times. */
    template <size_type reps>
    [[nodiscard]] static constexpr auto repeat() noexcept {
        return []<size_t I = 0>(this auto&& self) {
            if constexpr (I < reps) {
                return
                    std::forward<decltype(self)>(self).template operator()<I + 1>() +
                    string_list{};
            } else {
                return string_list<>{};
            }
        }();
    }

    /* Concatenate two string lists via the + operator. */
    template <static_str... OtherStrings>
    [[nodiscard]] constexpr auto operator+(string_list<OtherStrings...>) const noexcept {
        return string_list<Strings..., OtherStrings...>{};
    }

    /* Lexicographically compare two string lists. */
    template <static_str... OtherStrings>
    [[nodiscard]] constexpr std::strong_ordering operator<=>(
        string_list<OtherStrings...>
    ) const noexcept {
        return []<size_t I = 0>(this auto&& self) {
            if constexpr (I < sizeof...(Strings) && I < sizeof...(OtherStrings)) {
                auto comp =
                    meta::unpack_string<I, Strings...> <=>
                    meta::unpack_string<I, OtherStrings...>;
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
    [[nodiscard]] constexpr bool operator==(string_list<OtherStrings...>) const noexcept {
        if constexpr (sizeof...(OtherStrings) == size()) {
            return ((Strings == OtherStrings) && ...);
        } else {
            return false;
        }
    }

private:
    static constexpr std::array<value_type, size()> m_data {Strings...};
};


template <static_str... Keys> requires (meta::perfectly_hashable<Keys...>)
struct string_set : impl::string_set_tag {
    using value_type = const std::string_view;
    using reference = value_type&;
    using const_reference = reference;
    using pointer = value_type*;
    using const_pointer = pointer;
    using size_type = size_t;
    using index_type = ssize_t;
    using difference_type = std::ptrdiff_t;
    using hasher = impl::minimal_perfect_hash<Keys...>;
    using key_equal = std::equal_to<value_type>;
    using iterator = impl::hashed_string_iterator<value_type>;
    using const_iterator = iterator;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = reverse_iterator;
    using slice = impl::slice<iterator>;
    using const_slice = impl::slice<const_iterator>;

    template <typename T>
    static constexpr bool hashable = hasher::template hashable<T>;

    /* Swap the contents of two string sets.  Does nothing, since the strings are
    encoded entirely at compile time. */
    constexpr void swap(string_set&) noexcept {}

private:

    /* Get the value for a specific key within the table, where the key is known at
    runtime.  Throws a `KeyError` if the key is not present in the map. */
    template <size_t N>
    static constexpr index_type lookup(const char(&key)[N]) noexcept {
        constexpr size_t M = N - 1;
        if constexpr (M < hasher::min_length || M > hasher::max_length) {
            return -1;
        } else {
            size_t idx = hasher{}(key) % size();
            const_reference result = table[idx];
            if (M != result.size()) {
                return -1;
            }
            for (size_t i = 0; i < M; ++i) {
                if (key[i] != result[i]) {
                    return -1;
                }
            }
            return idx;
        }
    }

    /* Get the value for a specific key within the table, where the key is known at
    runtime.  Throws a `KeyError` if the key is not present in the map. */
    template <meta::static_str K>
    static constexpr index_type lookup(const K& key) noexcept {
        constexpr size_t len = K::size();
        if constexpr (len < hasher::min_length || len > hasher::max_length) {
            return -1;
        } else {
            index_type idx = hasher{}(key) % size();
            return key == table[idx] ? idx : -1;
        }
    }

    /* Get the value for a specific key within the table, where the key is known at
    runtime.  Throws a `KeyError` if the key is not present in the map. */
    template <meta::convertible_to<const char*> K>
        requires (!meta::string_literal<K> && !meta::static_str<K>)
    static constexpr index_type lookup(K&& key) noexcept(
        noexcept(static_cast<const char*>(std::forward<K>(key)))
    ) {
        const char* str = std::forward<K>(key);
        size_t len;
        index_type idx = hasher{}(str, len) % size();
        const_reference result = table[idx];
        if (len != result.size()) {
            return -1;
        }
        for (size_t i = 0; i < len; ++i) {
            if (str[i] != result[i]) {
                return -1;
            }
        }
        return idx;
    }

    /* Get the value for a specific key within the table, where the key is known at
    runtime.  Throws a `KeyError` if the key is not present in the map. */
    template <meta::convertible_to<std::string_view> K>
        requires (
            !meta::string_literal<K> &&
            !meta::static_str<K> &&
            !meta::convertible_to<K, const char*>
        )
    static constexpr index_type lookup(K&& key) noexcept(
        noexcept(std::string_view(std::forward<K>(key)))
    ) {
        std::string_view str = std::forward<K>(key);
        if (str.size() < hasher::min_length || str.size() > hasher::max_length) {
            return -1;
        }
        index_type idx = hasher{}(str) % size();
        return str == table[idx] ? idx : -1;
    }

    /* Get the value for a specific key within the table, where the key is known at
    runtime.  Throws a `KeyError` if the key is not present in the map. */
    template <meta::convertible_to<std::string> K>
        requires (
            !meta::string_literal<K> &&
            !meta::static_str<K> &&
            !meta::convertible_to<K, const char*> &&
            !meta::convertible_to<K, std::string_view>
        )
    static constexpr index_type lookup(K&& key) noexcept(
        noexcept(std::string(std::forward<K>(key)))
    ) {
        std::string str = std::forward<K>(key);
        if (str.size() < hasher::min_length || str.size() > hasher::max_length) {
            return -1;
        }
        index_type idx = hasher{}(str) % size();
        return str == table[idx] ? idx : -1;
    }

public:
    [[nodiscard]] static constexpr pointer data() noexcept { return table.data(); }
    [[nodiscard]] static constexpr size_type size() noexcept { return sizeof...(Keys); }
    [[nodiscard]] static constexpr index_type ssize() noexcept { return ssize_t(size()); }
    [[nodiscard]] static constexpr bool empty() noexcept { return size() == 0; }
    [[nodiscard]] explicit constexpr operator bool() noexcept { return !empty(); }
    [[nodiscard]] static constexpr iterator begin() noexcept {
        return {data(), hash_index.data(), 0, ssize()};
    }
    [[nodiscard]] static constexpr iterator cbegin() noexcept {
        return {data(), hash_index.data(), 0, ssize()};
    }
    [[nodiscard]] static constexpr iterator end() noexcept {
        return {data(), hash_index.data(), ssize(), ssize()};
    }
    [[nodiscard]] static constexpr iterator cend() noexcept {
        return {data(), hash_index.data(), ssize(), ssize()};
    }
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

    /* Check whether the set contains an arbitrary string. */
    template <static_str Key>
    [[nodiscard]] static constexpr bool contains() noexcept {
        return
            Key.size() >= hasher::min_length &&
            Key.size() <= hasher::max_length &&
            Key == meta::unpack_string<pack_index[hasher{}(Key) % size()], Keys...>;
    }

    /* Check whether the list contains an arbitrary string. */
    template <typename K> requires (hashable<K>)
    [[nodiscard]] static constexpr bool contains(K&& key) noexcept(
        noexcept(lookup(std::forward<K>(key)))
    ) {
        return lookup(std::forward<K>(key)) >= 0;
    }

    /* Get the index of a string within the set.  Fails to compile if the string is not
    present. */
    template <static_str Key> requires (contains<Key>())
    [[nodiscard]] static constexpr index_type index() noexcept {
        return index_type(pack_index[hasher{}(Key) % size()]);
    }

    /* Get the index of a string within the set.  Returns -1 if the string is not
    present in the set. */
    template <typename K> requires (hashable<K>)
    [[nodiscard]] static constexpr Optional<index_type> index(K&& key)
        noexcept(noexcept(lookup(std::forward<K>(key))))
    {
        index_type idx = lookup(std::forward<K>(key));
        if (idx < 0) {
            return None;
        }
        return index_type(pack_index[idx]);
    }

    /* Get the string at index I, where `I` is known at compile time.  Applies
    Python-style wraparound for negative indices, and fails to compile if the index
    is out of bounds after normalization. */
    template <index_type I> requires (impl::valid_index<ssize(), I>)
    [[nodiscard]] static constexpr const auto& get() noexcept {
        constexpr size_type idx = size_type(impl::normalize_index<ssize(), I>());
        return meta::unpack_string<idx, Keys...>;
    }

    /* Get a slice from the set at compile time.  Takes an explicitly-initialized
    `bertrand::slice` pack describing the start, stop, and step indices.  Each index
    can be omitted by initializing it to `bertrand::None` (aka `std::nullopt`), which
    is equivalent to an empty slice index in Python.  Applies Python-style wraparound
    to both `start` and `stop`. */
    template <bertrand::slice s>
    [[nodiscard]] static constexpr auto get() noexcept {
        static constexpr auto indices = s.normalize(ssize());
        return []<size_t... Is>(std::index_sequence<Is...>) {
            return string_set<
                meta::unpack_string<indices.start + Is * indices.step, Keys...>...
            >{};
        }(std::make_index_sequence<indices.length>{});
    }

    /* Get the string at index i, where `i` is known at runtime.  Applies Python-style
    wraparound for negative indices, and throws an `IndexError` if the index is out of
    bounds after normalization. */
    [[nodiscard]] static constexpr reference operator[](index_type i) noexcept(
        noexcept(impl::normalize_index(ssize(), i))
    ) {
        return table[hash_index[impl::normalize_index(ssize(), i)]];
    }

    /* Slice operator.  Takes an explicitly-initialized `bertrand::slice` pack
    describing the start, stop, and step indices, and returns a slice object containing
    the strings within the slice.  Each index can be omitted by initializing it to
    `bertrand::None` (aka `std::nullopt`), which is equivalent to an empty slice index
    in Python.  Applies Python-style wraparound to both `start` and `stop`. */
    [[nodiscard]] constexpr slice operator[](bertrand::slice s) const
        noexcept(noexcept(slice{*this, s.normalize(ssize())}))
    {
        return {*this, s.normalize(ssize())};
    }

    /* Get an iterator to the key at index `i`.  Applies Python-style wraparound for
    negative indices, and returns an `end()` iterator if the index is out of bounds
    after normalization. */
    [[nodiscard]] static constexpr iterator at(index_type i) noexcept {
        index_type index = i + ssize() * (i < 0);
        if (index < 0 || index >= ssize()) {
            return end();
        }
        return {data(), hash_index.data(), index, ssize()};
    }

    /* Get an iterator to a string within the set.  Returns an `end()` iterator if
    the string is not present. */
    template <static_str Key>
    [[nodiscard]] static constexpr iterator find() noexcept {
        constexpr index_type idx = index<Key>();
        if constexpr (idx < 0) {
            return end();
        } else {
            return {data(), hash_index.data(), index_type(pack_index[idx]), ssize()};
        }
    }

    /* Get an iterator to a string within the set.  Returns an `end()` iterator if
    the string is not present. */
    template <typename K> requires (hashable<K>)
    [[nodiscard]] static constexpr iterator find(K&& key) noexcept(
        noexcept(lookup(std::forward<K>(key)))
    ) {
        index_type idx = lookup(std::forward<K>(key));
        if (idx < 0) {
            return end();
        }
        return {data(), hash_index.data(), index_type(pack_index[idx]), ssize()};
    }

private:

    template <auto indices, index_type I, static_str... Strs>
    struct forward_remove {
        using type = forward_remove<
            indices,
            I + 1,
            Strs...,
            meta::unpack_string<I, Keys...>
        >::type;
    };
    template <auto indices, index_type I, static_str... Strs>
        requires (I >= indices.start && I < indices.stop)
    struct forward_remove<indices, I, Strs...> {
        template <index_type J>
        struct filter {
            using type = forward_remove<
                indices,
                I + 1,
                Strs...,
                meta::unpack_string<J, Keys...>
            >::type;
        };
        template <index_type J> requires ((J - indices.start) % indices.step == 0)
        struct filter<J> { using type = forward_remove<indices, I + 1, Strs...>::type; };
        using type = filter<I>::type;
    };
    template <auto indices, index_type I, static_str... Strs> requires (I == ssize())
    struct forward_remove<indices, I, Strs...> { using type = string_set<Strs...>; };

    template <auto indices, index_type I, static_str... Strs>
    struct backward_remove {
        using type = backward_remove<
            indices,
            I - 1,
            meta::unpack_string<I, Keys...>,
            Strs...
        >::type;
    };
    template <auto indices, index_type I, static_str... Strs>
        requires (I <= indices.start && I > indices.stop)
    struct backward_remove<indices, I, Strs...> {
        template <index_type J>
        struct filter {
            using type = backward_remove<
                indices,
                I - 1,
                meta::unpack_string<J, Keys...>,
                Strs...
            >::type;
        };
        template <index_type J> requires ((indices.start - J) % indices.step == 0)
        struct filter<J> { using type = backward_remove<indices, I - 1, Strs...>::type; };
        using type = filter<I>::type;
    };
    template <auto indices, index_type I, static_str... Strs> requires (I == -1)
    struct backward_remove<indices, I, Strs...> { using type = string_set<Strs...>; };

    template <typename Less>
    struct sort_helper {
        using type = std::pair<std::string_view, size_t>;
        static constexpr auto array = []<size_t... Is>(std::index_sequence<Is...>) {
            std::array<type, size()> out {type{Keys, Is}...};
            bertrand::sort<sort_helper>(out);
            return out;
        }(std::make_index_sequence<size()>{});
        template <size_t I, typename out>
        struct call;
        template <size_t I, static_str... Strs>
        struct call<I, string_list<Strs...>> { using type = string_set<Strs...>; };
        template <size_t I, static_str... Strs> requires (I < size())
        struct call<I, string_list<Strs...>> {
            using type = call<I + 1, string_list<
                Strs...,
                meta::unpack_string<array[I].second, Keys...>
            >>::type;
        };
        Less less;
        constexpr bool operator()(const type& a, const type& b) const noexcept {
            return less(a.first, b.first);
        }
    };

public:

    /* Return a new string set containing a sorted permutation of the current set,
    according to a less-than comparison function type accepting two
    `std::string_view`s. */
    template <meta::default_constructible Less = impl::Less>
        requires (meta::iter_sortable<Less, std::string_view*, std::string_view*>)
    [[nodiscard]] static constexpr auto sort() noexcept {
        return typename sort_helper<Less>::template call<0, string_list<>>::type{};
    }

    /* Remove a string from the set, returning a new set without that element.  Fails
    to compile if the string is not present. */
    template <static_str Key> requires (contains<Key>())
    [[nodiscard]] static constexpr auto remove() noexcept {
        constexpr size_type idx = size_type(index<Key>());
        return []<size_t... Prev, size_t... Next>(
            std::index_sequence<Prev...>,
            std::index_sequence<Next...>
        ) {
            return string_set<
                meta::unpack_string<Prev, Keys...>...,
                meta::unpack_string<idx + 1 + Next, Keys...>...
            >{};
        }(
            std::make_index_sequence<idx>{},
            std::make_index_sequence<size() - idx - 1>{}
        );
    }

    /* Remove the string at index I, returning a new set without that element.
    Applies Python-style wraparound for negative indices, and fails to compile if the
    index is out of bounds after normalization. */
    template <index_type I> requires (impl::valid_index<ssize(), I>)
    [[nodiscard]] static constexpr auto remove() noexcept {
        constexpr size_type idx = size_type(impl::normalize_index<ssize(), I>());
        return []<size_t... Prev, size_t... Next>(
            std::index_sequence<Prev...>,
            std::index_sequence<Next...>
        ) {
            return string_set<
                meta::unpack_string<Prev, Keys...>...,
                meta::unpack_string<idx + 1 + Next, Keys...>...
            >{};
        }(
            std::make_index_sequence<idx>{},
            std::make_index_sequence<size() - idx - 1>{}
        );
    }

    /* Remove a slice from the set, returning a new set without the sliced elements.
    Takes an explicitly-initialized `bertrand::slice` pack describing the start, stop,
    and step indices.  Each index can be omitted by initializing it to `bertrand::None`
    (aka `std::nullopt`), which is equivalent to an empty slice index in Python.
    Applies Python-style wraparound to both `start` and `stop`. */
    template <bertrand::slice s>
    [[nodiscard]] static constexpr auto remove() noexcept {
        static constexpr auto indices = s.normalize(ssize());
        if constexpr (indices.length == 0) {
            return string_set{};
        } else if constexpr (indices.step > 0) {
            return typename forward_remove<indices, 0>::type{};
        } else {
            return typename backward_remove<indices, ssize() - 1>::type{};
        }
    }

    /* Equivalent to Python `sep.join(keys...)` */
    template <static_str sep>
    [[nodiscard]] static constexpr auto join() noexcept {
        return string_wrapper<sep>::template join<Keys...>();
    }

    /* Convert this string set into a string list. */
    [[nodiscard]] static constexpr auto to_list() noexcept(
        noexcept(string_list<Keys...>{})
    ) {
        return string_list<Keys...>{};
    }

    /* Convert this string set into a string map with the given values. */
    template <typename... Args>
        requires (sizeof...(Args) == size() && meta::has_common_type<Args...>)
    [[nodiscard]] static constexpr auto to_map(Args&&... args) noexcept(
        noexcept(string_map<
            meta::remove_reference<meta::common_type<Args...>>,
            Keys...
        >{std::forward<Args>(args)...})
    ) {
        return string_map<
            meta::remove_reference<meta::common_type<Args...>>,
            Keys...
        >{std::forward<Args>(args)...};
    }

private:

    template <typename other, size_t I, static_str... Strs>
    struct get_union { using type = get_union<other, I + 1, Strs...>::type; };
    template <static_str... Ks, size_t I, static_str... Strs>
        requires (I < sizeof...(Ks) && !contains<meta::unpack_string<I, Ks...>>())
    struct get_union<string_set<Ks...>, I, Strs...> {
        using type = get_union<
            string_set<Ks...>,
            I + 1,
            Strs...,
            meta::unpack_string<I, Ks...>
        >::type;
    };
    template <typename other, size_t I, static_str... Strs> requires (I == other::size())
    struct get_union<other, I, Strs...> { using type = string_set<Strs...>; };

    template <typename other, size_t I, static_str... Strs>
    struct get_intersection { using type = get_intersection<other, I + 1, Strs...>::type; };
    template <static_str... Ks, size_t I, static_str... Strs>
        requires (I < sizeof...(Ks) && contains<meta::unpack_string<I, Ks...>>())
    struct get_intersection<string_set<Ks...>, I, Strs...> {
        using type = get_intersection<
            string_set<Ks...>,
            I + 1,
            Strs...,
            meta::unpack_string<I, Ks...>
        >::type;
    };
    template <typename other, size_t I, static_str... Strs> requires (I == other::size())
    struct get_intersection<other, I, Strs...> { using type = string_set<Strs...>; };

    template <typename other, size_t I, static_str... Strs>
    struct get_difference { using type = get_difference<other, I + 1, Strs...>::type; };
    template <typename other, size_t I, static_str... Strs>
        requires (I < size() && !other::template contains<meta::unpack_string<I, Keys...>>())
    struct get_difference<other, I, Strs...> {
        using type = get_difference<
            other,
            I + 1,
            Strs...,
            meta::unpack_string<I, Keys...>
        >::type;
    };
    template <typename other, size_t I, static_str... Strs> requires (I == size())
    struct get_difference<other, I, Strs...> { using type = string_set<Strs...>; };

public:

    /* Get the union of two string sets. */
    template <static_str... OtherKeys>
    [[nodiscard]] constexpr auto operator|(string_set<OtherKeys...>) const noexcept {
        return typename get_union<string_set<OtherKeys...>, 0, Keys...>::type{};
    }

    /* Get the intersection of two string sets. */
    template <static_str... OtherKeys>
    [[nodiscard]] constexpr auto operator&(string_set<OtherKeys...>) const noexcept {
        return typename get_intersection<string_set<OtherKeys...>, 0>::type{};
    }

    /* Get the difference between two string sets. */
    template <static_str... OtherKeys>
    [[nodiscard]] constexpr auto operator-(string_set<OtherKeys...>) const noexcept {
        return typename get_difference<string_set<OtherKeys...>, 0>::type{};
    }

    /* Get the symmetric difference between two string sets. */
    template <static_str... OtherKeys>
    [[nodiscard]] constexpr auto operator^(string_set<OtherKeys...>) const noexcept {
        return (*this - string_set<OtherKeys...>{}) | (string_set<OtherKeys...>{} - *this);
    }

    /* Check whether this string set is a strict subset of another set.  This is true
    if `set <= other` and `set != other`. */
    template <static_str... OtherKeys>
    [[nodiscard]] constexpr bool operator<(string_set<OtherKeys...>) const noexcept {
        return
            size() < sizeof...(OtherKeys) &&
            (string_set<OtherKeys...>::template contains<Keys>() && ...);
    }

    /* Check whether every string in this set is present in another set. */
    template <static_str... OtherKeys>
    [[nodiscard]] constexpr bool operator<=(string_set<OtherKeys...>) const noexcept {
        return (string_set<OtherKeys...>::template contains<Keys>() && ...);
    }

    /* Check two string sets for equality.  This is true if all keys are present in
    both sets, regardless of order. */
    template <static_str... OtherKeys>
    [[nodiscard]] constexpr bool operator==(string_set<OtherKeys...>) const noexcept {
        return (size() == sizeof...(OtherKeys)) && (contains<OtherKeys>() && ...);
    }

    /* Check two string sets for inequality.  Equivalent to `!(set == other)`. */
    template <static_str... OtherKeys>
    [[nodiscard]] constexpr bool operator!=(string_set<OtherKeys...>) const noexcept {
        return !(*this == string_set<OtherKeys...>{});
    }

    /* Check whether every string in another set is present in this set. */
    template <static_str... OtherKeys>
    [[nodiscard]] constexpr bool operator>=(string_set<OtherKeys...>) const noexcept {
        return (contains<OtherKeys>() && ...);
    }

    /* Check whether this string set is a strict superset of another set.  This is
    true if `set >= other` and `set != other`. */
    template <static_str... OtherKeys>
    [[nodiscard]] constexpr bool operator>(string_set<OtherKeys...>) const noexcept {
        return size() > sizeof...(OtherKeys) && (contains<OtherKeys>() && ...);
    }

private:
    static constexpr std::array<size_t, size()> hash_index {(hasher{}(Keys) % size())...};

    static constexpr auto pack_index = []<size_t... Is>(std::index_sequence<Is...>) {
        std::array<size_t, size()> out;
        ((out[hash_index[Is]] = Is), ...);
        return out;
    }(std::make_index_sequence<size()>{});

    static constexpr auto table = []<size_t... Is>(std::index_sequence<Is...>) {
        return std::array<value_type, size()>{
            meta::unpack_string<pack_index[Is], Keys...>...
        };
    }(std::make_index_sequence<size()>{});
};


template <meta::not_void T, static_str... Keys> requires (meta::perfectly_hashable<Keys...>)
struct string_map : impl::string_map_tag {
    using key_type = const std::string_view;
    using mapped_type = meta::remove_rvalue<T>;
    using value_type = std::pair<key_type, mapped_type>;
    using reference = meta::as_lvalue<value_type>;
    using const_reference = meta::as_const<reference>;
    using pointer = meta::as_pointer<reference>;
    using const_pointer = meta::as_pointer<const_reference>;
    using size_type = size_t;
    using index_type = ssize_t;
    using difference_type = std::ptrdiff_t;
    using hasher = impl::minimal_perfect_hash<Keys...>;
    using key_equal = std::equal_to<key_type>;
    using iterator = impl::hashed_string_iterator<value_type>;
    using const_iterator = impl::hashed_string_iterator<meta::as_const<value_type>>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using slice = impl::slice<iterator>;
    using const_slice = impl::slice<const_iterator>;

    template <typename U>
    static constexpr bool hashable = hasher::template hashable<U>;

    /* Swap the contents of two string maps.  Effectively swaps all the values without
    changing any keys. */
    constexpr void swap(string_map& other)
        noexcept((
            !meta::lvalue<mapped_type> &&
            meta::nothrow::swappable<mapped_type>
        ) || (
            !meta::lvalue<mapped_type> &&
            !meta::swappable<mapped_type> &&
            meta::nothrow::move_assignable<mapped_type>
        ) || (
            (
                meta::lvalue<mapped_type> ||
                !meta::swappable<mapped_type> ||
                !meta::move_assignable<mapped_type>
            ) &&
            meta::nothrow::movable<value_type> &&
            meta::nothrow::destructible<value_type>
        ))
        requires(
            (!meta::lvalue<mapped_type> && meta::swappable<mapped_type>) ||
            (!meta::lvalue<mapped_type> && meta::move_assignable<mapped_type>) ||
            (meta::movable<value_type> && meta::destructible<value_type>)
        )
    {
        if (this != &other) {
            for (size_t i = 0; i < size(); ++i) {
                if constexpr (
                    !meta::lvalue<mapped_type> &&
                    meta::swappable<mapped_type>
                ) {
                    std::ranges::swap(table[i].second, other.table[i].second);

                } else if constexpr (
                    !meta::lvalue<mapped_type> &&
                    meta::move_assignable<mapped_type>
                ) {
                    mapped_type temp = std::move(table[i].second);
                    try {
                        table[i].second = std::move(other.table[i].second);
                        try {
                            other.table[i].second = std::move(temp);
                        } catch (...) {
                            other.table[i].second = std::move(table[i].second);
                            throw;
                        }
                    } catch (...) {
                        table[i].second = std::move(temp);
                        throw;
                    }

                } else {
                    value_type temp = std::move(table[i]);
                    try {
                        std::destroy_at(&table[i]);
                        std::construct_at(&table[i], std::move(other.table[i]));
                        try {
                            std::destroy_at(&other.table[i]);
                            std::construct_at(&other.table[i], std::move(temp));
                        } catch (...) {
                            std::construct_at(&other.table[i], std::move(table[i]));
                            std::destroy_at(&table[i]);
                            throw;
                        }
                    } catch (...) {
                        std::construct_at(&table[i], std::move(temp));
                        throw;
                    }
                }
            }
        }
    }

private:
    using mapped_ref = meta::as_lvalue<mapped_type>;
    using const_mapped_ref = meta::as_const<mapped_ref>;

    template <size_t N>
    constexpr index_type lookup(const char(&key)[N]) const noexcept {
        constexpr size_t M = N - 1;
        if constexpr (M < hasher::min_length || M > hasher::max_length) {
            return -1;
        } else {
            size_t idx = hasher{}(key) % size();
            const_reference result = table[idx];
            if (M != result.first.size()) {
                return -1;
            }
            for (size_t i = 0; i < M; ++i) {
                if (key[i] != result.first[i]) {
                    return -1;
                }
            }
            return idx;
        }
    }

    template <meta::static_str K>
    constexpr index_type lookup(const K& key) const noexcept {
        constexpr size_t len = K::size();
        if constexpr (len < hasher::min_length || len > hasher::max_length) {
            return -1;
        } else {
            index_type idx = hasher{}(key) % size();
            return key == table[idx].first ? idx : -1;
        }
    }

    template <meta::convertible_to<const char*> K>
        requires (!meta::string_literal<K> && !meta::static_str<K>)
    constexpr index_type lookup(K&& key) const noexcept(
        noexcept(static_cast<const char*>(std::forward<K>(key)))
    ) {
        const char* str = std::forward<K>(key);
        size_t len;
        index_type idx = hasher{}(str, len) % size();
        const_reference result = table[idx];
        if (len != result.first.size()) {
            return -1;
        }
        for (size_t i = 0; i < len; ++i) {
            if (str[i] != result.first[i]) {
                return -1;
            }
        }
        return idx;
    }

    template <meta::convertible_to<std::string_view> K>
        requires (
            !meta::string_literal<K> &&
            !meta::static_str<K> &&
            !meta::convertible_to<K, const char*>
        )
    constexpr index_type lookup(K&& key) const noexcept(
        noexcept(std::string_view(std::forward<K>(key)))
    ) {
        std::string_view str = std::forward<K>(key);
        if (str.size() < hasher::min_length || str.size() > hasher::max_length) {
            return -1;
        }
        index_type idx = hasher{}(str) % size();
        return str == table[idx].first ? idx : -1;
    }

    template <meta::convertible_to<std::string> K>
        requires (
            !meta::string_literal<K> &&
            !meta::static_str<K> &&
            !meta::convertible_to<K, const char*> &&
            !meta::convertible_to<K, std::string_view>
        )
    constexpr index_type lookup(K&& key) const noexcept(
        noexcept(std::string(std::forward<K>(key)))
    ) {
        std::string str = std::forward<K>(key);
        if (str.size() < hasher::min_length || str.size() > hasher::max_length) {
            return -1;
        }
        index_type idx = hasher{}(str) % size();
        return str == table[idx].first ? idx : -1;
    }

public:
    [[nodiscard]] constexpr pointer data() noexcept { return table.data(); }
    [[nodiscard]] constexpr const_pointer data() const noexcept { return table.data(); }
    [[nodiscard]] static constexpr size_type size() noexcept { return sizeof...(Keys); }
    [[nodiscard]] static constexpr index_type ssize() noexcept { return ssize_t(size()); }
    [[nodiscard]] static constexpr bool empty() noexcept { return size() == 0; }
    [[nodiscard]] explicit constexpr operator bool() noexcept { return !empty(); }
    [[nodiscard]] constexpr iterator begin() noexcept {
        return {data(), hash_index.data(), 0, ssize()};
    }
    [[nodiscard]] constexpr const_iterator begin() const noexcept {
        return {data(), hash_index.data(), 0, ssize()};
    }
    [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
        return {data(), hash_index.data(), 0, ssize()};
    }
    [[nodiscard]] constexpr iterator end() noexcept {
        return {data(), hash_index.data(), ssize(), ssize()};
    }
    [[nodiscard]] constexpr const_iterator end() const noexcept {
        return {data(), hash_index.data(), ssize(), ssize()};
    }
    [[nodiscard]] constexpr const_iterator cend() const noexcept {
        return {data(), hash_index.data(), ssize(), ssize()};
    }
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

    /* Check whether the set contains an arbitrary string. */
    template <static_str Key>
    [[nodiscard]] static constexpr bool contains() noexcept {
        return
            Key.size() >= hasher::min_length &&
            Key.size() <= hasher::max_length &&
            Key == meta::unpack_string<pack_index[hasher{}(Key) % size()], Keys...>;
    }

    /* Check whether the list contains an arbitrary string. */
    template <typename K> requires (hashable<K>)
    [[nodiscard]] constexpr bool contains(K&& key) const noexcept(
        noexcept(lookup(std::forward<K>(key)))
    ) {
        return lookup(std::forward<K>(key)) >= 0;
    }

    /* Get the index of a string within the set.  Fails to compile if the string is not
    present. */
    template <static_str Key> requires (contains<Key>())
    [[nodiscard]] static constexpr index_type index() noexcept {
        return index_type(pack_index[hasher{}(Key) % size()]);
    }

    /* Get the index of a string within the set.  Returns -1 if the string is not
    present in the set. */
    template <typename K> requires (hashable<K>)
    [[nodiscard]] constexpr Optional<index_type> index(K&& key) const
        noexcept(noexcept(lookup(std::forward<K>(key))))
    {
        index_type idx = lookup(std::forward<K>(key));
        if (idx < 0) {
            return None;
        }
        return index_type(pack_index[idx]);
    }

    /* Get the value for a specific key within the table, where the key is known at
    compile time.  Fails to compile if the key is not present in the map. */
    template <static_str K> requires (contains<K>())
    [[nodiscard]] constexpr mapped_ref get() & noexcept {
        constexpr size_type idx = hasher{}(K) % size();
        return table[idx].second;
    }

    /* Get the value for a specific key within the table, where the key is known at
    compile time.  Fails to compile if the key is not present in the map. */
    template <static_str K> requires (contains<K>())
    [[nodiscard]] constexpr mapped_type get() && noexcept(
        noexcept(mapped_type(std::forward<mapped_type>(
            table[hasher{}(K) % size()].second
        )))
    ) {
        constexpr size_type idx = hasher{}(K) % size();
        return std::forward<mapped_type>(table[idx].second);
    }

    /* Get the value for a specific key within the table, where the key is known at
    compile time.  Fails to compile if the key is not present in the map. */
    template <static_str K> requires (contains<K>())
    [[nodiscard]] constexpr const_mapped_ref get() const noexcept {
        constexpr size_type idx = hasher{}(K) % size();
        return table[idx].second;
    }

    /* Get the value for the key at index I, where `I` is known at compile time.
    Applies Python-style wraparound for negative indices, and fails to compile if the
    index is out of bounds after normalization. */
    template <index_type I> requires (impl::valid_index<ssize(), I>)
    [[nodiscard]] constexpr mapped_ref get() & noexcept {
        constexpr size_type idx = size_type(impl::normalize_index<ssize(), I>());
        return table[hash_index[idx]].second;
    }

    /* Get the value for the key at index I, where `I` is known at compile time.
    Applies Python-style wraparound for negative indices, and fails to compile if the
    index is out of bounds after normalization. */
    template <index_type I> requires (impl::valid_index<ssize(), I>)
    [[nodiscard]] constexpr mapped_type get() && noexcept(
        noexcept(mapped_type(std::forward<mapped_type>(
            table[hash_index[impl::normalize_index<ssize(), I>()]].second
        )))
    ) {
        constexpr size_type idx = size_type(impl::normalize_index<ssize(), I>());
        return std::forward<mapped_type>(table[hash_index[idx]].second);
    }

    /* Get the value for the key at index I, where `I` is known at compile time.
    Applies Python-style wraparound for negative indices, and fails to compile if the
    index is out of bounds after normalization. */
    template <index_type I> requires (impl::valid_index<ssize(), I>)
    [[nodiscard]] constexpr const_mapped_ref get() const noexcept {
        constexpr size_type idx = size_type(impl::normalize_index<ssize(), I>());
        return table[hash_index[idx]].second;
    }

    /* Get a slice from the map at compile time.  Takes an explicitly-initialized
    `bertrand::slice` pack describing the start, stop, and step indices.  Each index
    can be omitted by initializing it to `bertrand::None` (aka `std::nullopt`), which
    is equivalent to an empty slice index in Python.  Applies Python-style wraparound
    to both `start` and `stop`. */
    template <bertrand::slice s>
    [[nodiscard]] constexpr auto get() const noexcept(meta::nothrow::copyable<mapped_type>) {
        static constexpr auto indices = s.normalize(ssize());
        return [this]<size_t... Is>(std::index_sequence<Is...>) {
            return string_map<
                mapped_type,
                meta::unpack_string<indices.start + Is * indices.step, Keys>...
            >{get<indices.start + Is * indices.step>()...};
        }(std::make_index_sequence<indices.length>{});
    }

    /* Get a slice from the map at compile time.  Takes an explicitly-initialized
    `bertrand::slice` pack describing the start, stop, and step indices.  Each index
    can be omitted by initializing it to `bertrand::None` (aka `std::nullopt`), which
    is equivalent to an empty slice index in Python.  Applies Python-style wraparound
    to both `start` and `stop`. */
    template <bertrand::slice s>
    [[nodiscard]] constexpr auto get() && noexcept(meta::nothrow::movable<mapped_type>) {
        static constexpr auto indices = s.normalize(ssize());
        return [this]<size_t... Is>(std::index_sequence<Is...>) {
            return string_map<
                mapped_type,
                meta::unpack_string<indices.start + Is * indices.step, Keys>...
            >{get<indices.start + Is * indices.step>()...};
        }(std::make_index_sequence<indices.length>{});
    }

    /// TODO: maybe the bounds checks can be optimized out in non-debug builds?

    /* Get the value for a specific key within the table, where the key is known at
    runtime.  Throws a `KeyError` if the key is not present in the map. */
    template <typename K> requires (hashable<K>)
    [[nodiscard]] constexpr mapped_ref operator[](K&& key) & {
        index_type idx = lookup(std::forward<K>(key));
        if (idx < 0) {
            throw KeyError(key);
        }
        return table[idx].second;
    }

    /* Get the value for a specific key within the table, where the key is known at
    runtime.  Throws a `KeyError` if the key is not present in the map. */
    template <typename K> requires (hashable<K>)
    [[nodiscard]] constexpr mapped_type operator[](K&& key) && {
        index_type idx = lookup(std::forward<K>(key));
        if (idx < 0) {
            throw KeyError(key);
        }
        return std::forward<mapped_type>(table[idx].second);
    }

    /* Get the value for a specific key within the table, where the key is known at
    runtime.  Throws a `KeyError` if the key is not present in the map. */
    template <typename K> requires (hashable<K>)
    [[nodiscard]] constexpr const_mapped_ref operator[](K&& key) const {
        index_type idx = lookup(std::forward<K>(key));
        if (idx < 0) {
            throw KeyError(key);
        }
        return table[idx].second;
    }

    /* Get the value for the key at index i, where `i` is known at runtime.  Applies
    Python-style wraparound for negative indices, and throws an `IndexError` if the
    index is out of bounds after normalization. */
    [[nodiscard]] constexpr mapped_ref operator[](index_type i) & noexcept(
        noexcept(impl::normalize_index(ssize(), i))
    ) {
        return table[hash_index[impl::normalize_index(ssize(), i)]].second;
    }

    /* Get the value for the key at index i, where `i` is known at runtime.  Applies
    Python-style wraparound for negative indices, and throws an `IndexError` if the
    index is out of bounds after normalization. */
    [[nodiscard]] constexpr mapped_type operator[](index_type i) && noexcept(
        noexcept(mapped_type(std::forward<mapped_type>(
            table[hash_index[impl::normalize_index(ssize(), i)]].second
        )))
    ) {
        return std::forward<mapped_type>(
            table[hash_index[impl::normalize_index(ssize(), i)]].second
        );
    }

    /* Get the value for the key at index i, where `i` is known at runtime.  Applies
    Python-style wraparound for negative indices, and throws an `IndexError` if the
    index is out of bounds after normalization. */
    [[nodiscard]] constexpr const_mapped_ref operator[](index_type i) const noexcept(
        noexcept(impl::normalize_index(ssize(), i))
    ) {
        return table[hash_index[impl::normalize_index(ssize(), i)]].second;
    }

    /* Slice operator.  Takes an explicitly-initialized `bertrand::slice` pack
    describing the start, stop, and step indices, and returns a slice object containing
    the strings within the slice.  Each index can be omitted by initializing it to
    `bertrand::None` (aka `std::nullopt`), which is equivalent to an empty slice index
    in Python.  Applies Python-style wraparound to both `start` and `stop`. */
    [[nodiscard]] constexpr slice operator[](bertrand::slice s) noexcept(
        noexcept(slice{*this, s.normalize(ssize())})
    ) {
        return {*this, s.normalize(ssize())};
    }

    /* Slice operator.  Takes an explicitly-initialized `bertrand::slice` pack
    describing the start, stop, and step indices, and returns a slice object containing
    the strings within the slice.  Each index can be omitted by initializing it to
    `bertrand::None` (aka `std::nullopt`), which is equivalent to an empty slice index
    in Python.  Applies Python-style wraparound to both `start` and `stop`. */
    [[nodiscard]] constexpr const_slice operator[](bertrand::slice s) const noexcept(
        noexcept(const_slice{*this, s.normalize(ssize())})
    ) {
        return {*this, s.normalize(ssize())};
    }

    /* Get an iterator to the key-value pair at index `i`.  Applies Python-style
    wraparound for negative indices, and returns an `end()` iterator if the index is
    out of bounds after normalization. */
    [[nodiscard]] constexpr iterator at(index_type i) noexcept {
        index_type index = i + ssize() * (i < 0);
        if (index < 0 || index >= ssize()) {
            return end();
        } else {
            return {data(), hash_index.data(), index, ssize()};
        }
    }

    /* Get an iterator to the key-value pair at index `i`. Applies Python-style
    wraparound for negative indices, and returns an `end()` iterator if the index is
    out of bounds after normalization. */
    [[nodiscard]] constexpr const_iterator at(index_type i) const noexcept {
        index_type index = i + ssize() * (i < 0);
        if (index < 0 || index >= ssize()) {
            return end();
        } else {
            return {data(), hash_index.data(), index, ssize()};
        }
    }

    /* Get an iterator to a string within the set.  Returns an `end()` iterator if
    the string is not present. */
    template <static_str Key>
    [[nodiscard]] constexpr iterator find() noexcept {
        constexpr index_type idx = index<Key>();
        if constexpr (idx < 0) {
            return end();
        } else {
            return {data(), hash_index.data(), index_type(pack_index[idx]), ssize()};
        }
    }

    /* Get an iterator to a string within the set.  Returns an `end()` iterator if
    the string is not present. */
    template <static_str Key>
    [[nodiscard]] constexpr const_iterator find() const noexcept {
        constexpr index_type idx = index<Key>();
        if constexpr (idx < 0) {
            return end();
        } else {
            return {data(), hash_index.data(), index_type(pack_index[idx]), ssize()};
        }
    }

    /* Get an iterator to a string within the set.  Returns an `end()` iterator if
    the string is not present. */
    template <typename K> requires (hashable<K>)
    [[nodiscard]] constexpr iterator find(K&& key) noexcept(
        noexcept(lookup(std::forward<K>(key)))
    ) {
        index_type idx = lookup(std::forward<K>(key));
        if (idx < 0) {
            return end();
        }
        return {data(), hash_index.data(), index_type(pack_index[idx]), ssize()};
    }

    /* Get an iterator to a string within the set.  Returns an `end()` iterator if
    the string is not present. */
    template <typename K> requires (hashable<K>)
    [[nodiscard]] constexpr const_iterator find(K&& key) const noexcept(
        noexcept(lookup(std::forward<K>(key)))
    ) {
        index_type idx = lookup(std::forward<K>(key));
        if (idx < 0) {
            return end();
        }
        return {data(), hash_index.data(), index_type(pack_index[idx]), ssize()};
    }

private:

    template <auto indices, index_type I, static_str... Strs>
    struct forward_remove {
        using type = forward_remove<
            indices,
            I + 1,
            Strs...,
            meta::unpack_string<I, Keys...>
        >::type;
        static constexpr type operator()(auto&& self, auto&&... args) noexcept(
            noexcept(forward_remove<
                indices,
                I + 1,
                Strs...,
                meta::unpack_string<I, Keys...>
            >{}(
                std::forward<decltype(self)>(self),
                std::forward<decltype(args)>(args)...,
                std::forward<decltype(self)>(self).template get<I>()
            ))
        ) {
            return forward_remove<
                indices,
                I + 1,
                Strs...,
                meta::unpack_string<I, Keys...>
            >{}(
                std::forward<decltype(self)>(self),
                std::forward<decltype(args)>(args)...,
                std::forward<decltype(self)>(self).template get<I>()
            );
        }
    };
    template <auto indices, index_type I, static_str... Strs>
        requires (I >= indices.start && I < indices.stop)
    struct forward_remove<indices, I, Strs...> {
        template <index_type J>
        struct filter {
            using type = forward_remove<
                indices,
                I + 1,
                Strs...,
                meta::unpack_string<J, Keys...>
            >::type;
            static constexpr type operator()(auto&& self, auto&&... args) noexcept(
                noexcept(forward_remove<
                    indices,
                    I + 1,
                    Strs...,
                    meta::unpack_string<J, Keys...>
                >{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(args)>(args)...,
                    std::forward<decltype(self)>(self).template get<J>()
                ))
            ) {
                return forward_remove<
                    indices,
                    I + 1,
                    Strs...,
                    meta::unpack_string<J, Keys...>
                >{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(args)>(args)...,
                    std::forward<decltype(self)>(self).template get<J>()
                );
            }
        };

        template <index_type J> requires ((J - indices.start) % indices.step == 0)
        struct filter<J> {
            using type = forward_remove<indices, I + 1, Strs...>::type;
            static constexpr type operator()(auto&& self, auto&&... args) noexcept(
                noexcept(forward_remove<indices, I + 1, Strs...>{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(args)>(args)...
                ))
            ) {
                return forward_remove<indices, I + 1, Strs...>{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(args)>(args)...
                );
            }
        };

        using type = filter<I>::type;
        static constexpr type operator()(auto&& self, auto&&... args) noexcept(
            noexcept(filter<I>{}(
                std::forward<decltype(self)>(self),
                std::forward<decltype(args)>(args)...
            ))
        ) {
            return filter<I>{}(
                std::forward<decltype(self)>(self),
                std::forward<decltype(args)>(args)...
            );
        }
    };
    template <auto indices, index_type I, static_str... Strs> requires (I == ssize())
    struct forward_remove<indices, I, Strs...> {
        using type = string_map<mapped_type, Strs...>;
        static constexpr type operator()(auto&& self, auto&&... args) noexcept(
            noexcept(type{std::forward<decltype(args)>(args)...})
        ) {
            return type{std::forward<decltype(args)>(args)...};
        }
    };

    template <auto indices, index_type I, static_str... Strs>
    struct backward_remove {
        using type = backward_remove<
            indices,
            I - 1,
            meta::unpack_string<I, Keys...>,
            Strs...
        >::type;
        static constexpr type operator()(auto&& self, auto&&... args) noexcept(
            noexcept(backward_remove<
                indices,
                I - 1,
                meta::unpack_string<I, Keys...>,
                Strs...
            >{}(
                std::forward<decltype(self)>(self),
                std::forward<decltype(self)>(self).template get<I>(),
                std::forward<decltype(args)>(args)...
            ))
        ) {
            return backward_remove<
                indices,
                I - 1,
                meta::unpack_string<I, Keys...>,
                Strs...
            >{}(
                std::forward<decltype(self)>(self),
                std::forward<decltype(self)>(self).template get<I>(),
                std::forward<decltype(args)>(args)...
            );
        }
    };
    template <auto indices, index_type I, static_str... Strs>
        requires (I <= indices.start && I > indices.stop)
    struct backward_remove<indices, I, Strs...> {
        template <index_type J>
        struct filter {
            using type = backward_remove<
                indices,
                I - 1,
                meta::unpack_string<J, Keys...>,
                Strs...
            >::type;
            static constexpr type operator()(auto&& self, auto&&... args) noexcept(
                noexcept(backward_remove<
                    indices,
                    I - 1,
                    meta::unpack_string<J, Keys...>,
                    Strs...
                >{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(self)>(self).template get<J>(),
                    std::forward<decltype(args)>(args)...
                ))
            ) {
                return backward_remove<
                    indices,
                    I - 1,
                    meta::unpack_string<J, Keys...>,
                    Strs...
                >{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(self)>(self).template get<J>(),
                    std::forward<decltype(args)>(args)...
                );
            }
        };

        template <index_type J> requires ((indices.start - J) % indices.step == 0)
        struct filter<J> {
            using type = backward_remove<indices, I - 1, Strs...>::type;
            static constexpr type operator()(auto&& self, auto&&... args) noexcept(
                noexcept(backward_remove<indices, I - 1, Strs...>{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(args)>(args)...
                ))
            ) {
                return backward_remove<indices, I - 1, Strs...>{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(args)>(args)...
                );
            }
        };

        using type = filter<I>::type;
        static constexpr type operator()(auto&& self, auto&&... args) noexcept(
            noexcept(filter<I>{}(
                std::forward<decltype(self)>(self),
                std::forward<decltype(args)>(args)...
            ))
        ) {
            return filter<I>{}(
                std::forward<decltype(self)>(self),
                std::forward<decltype(args)>(args)...
            );
        }
    };
    template <auto indices, index_type I, static_str... Strs> requires (I == -1)
    struct backward_remove<indices, I, Strs...> {
        using type = string_map<mapped_type, Strs...>;
        static constexpr type operator()(auto&& self, auto&&... args) noexcept(
            noexcept(type{std::forward<decltype(args)>(args)...})
        ) {
            return type{std::forward<decltype(args)>(args)...};
        }
    };

    template <size_t idx, typename Self, size_t... Prev, size_t... Next>
    constexpr auto do_remove(
        this Self&& self,
        std::index_sequence<Prev...>,
        std::index_sequence<Next...>
    )
        noexcept(noexcept(string_map<
            mapped_type,
            meta::unpack_string<Prev, Keys...>...,
            meta::unpack_string<idx + 1 + Next, Keys...>...
        >{
            std::forward<Self>(self).template get<Prev>()...,
            std::forward<Self>(self).template get<idx + 1 + Next>()...
        }))
        requires(requires{string_map<
            mapped_type,
            meta::unpack_string<Prev, Keys...>...,
            meta::unpack_string<idx + 1 + Next, Keys...>...
        >{
            std::forward<Self>(self).template get<Prev>()...,
            std::forward<Self>(self).template get<idx + 1 + Next>()...
        };})
    {
        return string_map<
            mapped_type,
            meta::unpack_string<Prev, Keys...>...,
            meta::unpack_string<idx + 1 + Next, Keys...>...
        >{
            std::forward<Self>(self).template get<Prev>()...,
            std::forward<Self>(self).template get<idx + 1 + Next>()...
        };
    }

    template <typename Less>
    struct sort_helper {
        using type = std::pair<std::string_view, size_t>;
        static constexpr auto array = []<size_t... Is>(std::index_sequence<Is...>) {
            std::array<type, size()> out {type{Keys, Is}...};
            bertrand::sort<sort_helper>(out);
            return out;
        }(std::make_index_sequence<size()>{});
        template <size_t I, typename out>
        struct call;
        template <size_t I, static_str... Strs>
        struct call<I, string_list<Strs...>> {
            using type = string_map<mapped_type, Strs...>;
            static constexpr type operator()(auto&& self, auto&&... args)
                noexcept(meta::nothrow::constructible_from<type, decltype(args)...>)
                requires(meta::constructible_from<type, decltype(args)...>)
            {
                return {std::forward<decltype(args)>(args)...};
            }
        };
        template <size_t I, static_str... Strs> requires (I < size())
        struct call<I, string_list<Strs...>> {
            using type = call<I + 1, string_list<
                Strs...,
                meta::unpack_string<array[I].second, Keys...>
            >>::type;
            static constexpr decltype(auto) operator()(auto&& self, auto&&... args)
                noexcept(noexcept(call<I + 1, string_list<
                    Strs...,
                    meta::unpack_string<array[I].second, Keys...>
                >>{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(args)>(args)...,
                    std::forward<decltype(self)>(self).template get<array[I].second>()
                )))
                requires(requires{call<I + 1, string_list<
                    Strs...,
                    meta::unpack_string<array[I].second, Keys...>
                >>{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(args)>(args)...,
                    std::forward<decltype(self)>(self).template get<array[I].second>()
                );})
            {
                return (call<I + 1, string_list<
                    Strs...,
                    meta::unpack_string<array[I].second, Keys...>
                >>{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(args)>(args)...,
                    std::forward<decltype(self)>(self).template get<array[I].second>()
                ));
            }
        };
        Less less;
        constexpr bool operator()(const type& a, const type& b) const noexcept {
            return less(a.first, b.first);
        }
    };

public:

    /* Return a new string map containing a sorted permutation of the current map,
    according to a less-than comparison function type accepting two
    `std::string_view`s. */
    template <meta::default_constructible Less = impl::Less, typename Self>
        requires (meta::iter_sortable<Less, std::string_view*, std::string_view*>)
    [[nodiscard]] constexpr auto sort(this Self&& self)
        noexcept(meta::nothrow::callable<
            typename sort_helper<Less>::template call<0, string_list<>>,
            Self    
        >)
        requires(meta::callable<
            typename sort_helper<Less>::template call<0, string_list<>>,
            Self
        >)
    {
        return typename sort_helper<Less>::template call<0, string_list<>>{}(
            std::forward<Self>(self)
        );
    }

    /* Remove a string from the set, returning a new set without that element.  Fails
    to compile if the string is not present. */
    template <static_str Key, typename Self> requires (contains<Key>())
    [[nodiscard]] constexpr auto remove(this Self&& self)
        noexcept(noexcept(std::forward<Self>(self).template do_remove<size_type(index<Key>())>(
            std::make_index_sequence<size_type(index<Key>())>{},
            std::make_index_sequence<size() - size_type(index<Key>()) - 1>{}
        )))
        requires(requires{std::forward<Self>(self).template do_remove<size_type(index<Key>())>(
            std::make_index_sequence<size_type(index<Key>())>{},
            std::make_index_sequence<size() - size_type(index<Key>()) - 1>{}
        );})
    {
        constexpr size_type idx = size_type(index<Key>());
        return std::forward<Self>(self).template do_remove<idx>(
            std::make_index_sequence<idx>{},
            std::make_index_sequence<size() - idx - 1>{}
        );
    }

    /* Remove the string at index I, returning a new set without that element.
    Applies Python-style wraparound for negative indices, and fails to compile if the
    index is out of bounds after normalization. */
    template <index_type I, typename Self> requires (impl::valid_index<ssize(), I>)
    [[nodiscard]] constexpr auto remove(this Self&& self)
        noexcept(noexcept(std::forward<Self>(self).template do_remove<
            size_type(impl::normalize_index<ssize(), I>())
        >(
            std::make_index_sequence<size_type(impl::normalize_index<ssize(), I>())>{},
            std::make_index_sequence<
                size() - size_type(impl::normalize_index<ssize(), I>()) - 1
            >{}
        )))
        requires(requires{std::forward<Self>(self).template do_remove<
            size_type(impl::normalize_index<ssize(), I>())
        >(
            std::make_index_sequence<size_type(impl::normalize_index<ssize(), I>())>{},
            std::make_index_sequence<
                size() - size_type(impl::normalize_index<ssize(), I>()) - 1
            >{}
        );})
    {
        constexpr size_type idx = size_type(impl::normalize_index<ssize(), I>());
        return std::forward<Self>(self).template do_remove<idx>(
            std::make_index_sequence<idx>{},
            std::make_index_sequence<size() - idx - 1>{}
        );
    }

    /* Remove a slice from the set, returning a new set without the sliced elements.
    Takes an explicitly-initialized `bertrand::slice` pack describing the start, stop,
    and step indices.  Each index can be omitted by initializing it to `bertrand::None`
    (aka `std::nullopt`), which is equivalent to an empty slice index in Python.
    Applies Python-style wraparound to both `start` and `stop`. */
    template <bertrand::slice s, typename Self>
    [[nodiscard]] constexpr auto remove(this Self&& self) noexcept(
        noexcept(forward_remove<
            s.normalize(ssize()),
            0
        >{}(std::forward<Self>(self))) &&
        noexcept(backward_remove<
            s.normalize(ssize()),
            ssize() - 1
        >{}(std::forward<Self>(self)))
    ) {
        static constexpr auto indices = s.normalize(ssize());
        if constexpr (indices.length == 0) {
            return std::forward<Self>(self);
        } else if constexpr (indices.step > 0) {
            return forward_remove<indices, 0>{}(std::forward<Self>(self));
        } else {
            return backward_remove<indices, ssize() - 1>{}(std::forward<Self>(self));
        }
    }

    /* Equivalent to Python `sep.join(keys...)` */
    template <static_str sep>
    [[nodiscard]] static constexpr auto join() noexcept {
        return string_wrapper<sep>::template join<Keys...>();
    }

    /* Convert this string set into a string list. */
    [[nodiscard]] static constexpr auto to_list() noexcept(
        noexcept(string_list<Keys...>{})
    ) {
        return string_list<Keys...>{};
    }

    /* Convert this string set into a string list. */
    [[nodiscard]] static constexpr auto to_set() noexcept(
        noexcept(string_set<Keys...>{})
    ) {
        return string_set<Keys...>{};
    }

private:

    template <size_t I, size_t J, typename Other, static_str... out>
    struct get_union {
        using type = string_map<
            meta::common_type<mapped_type, typename Other::mapped_type>,
            out...
        >;
        static constexpr type operator()(
            auto&& self,
            auto&& other,
            auto&&... args
        ) noexcept(
            noexcept(type{std::forward<decltype(args)>(args)...})
        ) {
            return {std::forward<decltype(args)>(args)...};
        }
    };
    template <size_t I, size_t J, typename U, static_str... Ks, static_str... out>
        requires (I < size())
    struct get_union<I, J, string_map<U, Ks...>, out...> {
        using type = get_union<
            I + 1,
            J,
            string_map<U, Ks...>,
            out...,
            meta::unpack_string<I, Keys...>
        >::type;

        template <static_str K>
        struct filter {
            static constexpr decltype(auto) operator()(auto&& self, auto&& other) noexcept(
                noexcept(std::forward<decltype(self)>(self).template get<K>())
            ) {
                return std::forward<decltype(self)>(self).template get<K>();
            }
        };
        template <static_str K> requires (string_map<U, Ks...>::template contains<K>())
        struct filter<K> {
            static constexpr decltype(auto) operator()(auto&& self, auto&& other) noexcept(
                noexcept(std::forward<decltype(other)>(other).template get<K>())
            ) {
                return std::forward<decltype(other)>(other).template get<K>();
            }
        };

        static constexpr type operator()(auto&& self, auto&& other, auto&&... args) noexcept(
            noexcept(get_union<
                I + 1,
                J,
                string_map<U, Ks...>,
                out...,
                meta::unpack_string<I, Keys...>
            >{}(
                std::forward<decltype(self)>(self),
                std::forward<decltype(other)>(other),
                std::forward<decltype(args)>(args)...,
                filter<meta::unpack_string<I, Keys...>>{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(other)>(other)
                )
            ))
        ) {
            return get_union<
                I + 1,
                J,
                string_map<U, Ks...>,
                out...,
                meta::unpack_string<I, Keys...>
            >{}(
                std::forward<decltype(self)>(self),
                std::forward<decltype(other)>(other),
                std::forward<decltype(args)>(args)...,
                filter<meta::unpack_string<I, Keys...>>{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(other)>(other)
                )
            );
        }
    };
    template <size_t J, typename U, static_str... Ks, static_str... out>
        requires (J < sizeof...(Ks))
    struct get_union<size(), J, string_map<U, Ks...>, out...> {
        template <static_str K>
        struct filter {
            using type = get_union<size(), J + 1, string_map<U, Ks...>, out..., K>::type;
            static constexpr type operator()(auto&& self, auto&& other, auto&&... args) noexcept(
                noexcept(get_union<size(), J + 1, string_map<U, Ks...>, out..., K>{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(other)>(other),
                    std::forward<decltype(args)>(args)...,
                    std::forward<decltype(other)>(other).template get<J>()
                ))
            ) {
                return get_union<size(), J + 1, string_map<U, Ks...>, out..., K>{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(other)>(other),
                    std::forward<decltype(args)>(args)...,
                    std::forward<decltype(other)>(other).template get<J>()
                );
            }
        };
        template <static_str K> requires (contains<K>())
        struct filter<K> {
            using type = get_union<size(), J + 1, string_map<U, Ks...>, out...>::type;
            static constexpr type operator()(auto&& self, auto&& other, auto&&... args) noexcept(
                noexcept(get_union<size(), J + 1, string_map<U, Ks...>, out...>{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(other)>(other),
                    std::forward<decltype(args)>(args)...
                ))
            ) {
                return get_union<size(), J + 1, string_map<U, Ks...>, out...>{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(other)>(other),
                    std::forward<decltype(args)>(args)...
                );
            }
        };
        using type = filter<meta::unpack_string<J, Ks...>>::type;
        static constexpr type operator()(auto&& self, auto&& other, auto&&... args) noexcept(
            noexcept(filter<meta::unpack_string<J, Ks...>>{}(
                std::forward<decltype(self)>(self),
                std::forward<decltype(other)>(other),
                std::forward<decltype(args)>(args)...
            ))
        ) {
            return filter<meta::unpack_string<J, Ks...>>{}(
                std::forward<decltype(self)>(self),
                std::forward<decltype(other)>(other),
                std::forward<decltype(args)>(args)...
            );
        }
    };

    template <size_t I, typename Other, static_str... out>
    struct get_intersection {
        using type = string_map<typename Other::mapped_type, out...>;
        static constexpr type operator()(auto&& other, auto&&... args) noexcept(
            noexcept(type{std::forward<decltype(args)>(args)...})
        ) {
            return {std::forward<decltype(args)>(args)...};
        }
    };
    template <size_t I, typename U, static_str... Ks, static_str... out>
        requires (I < sizeof...(Ks))
    struct get_intersection<I, string_map<U, Ks...>, out...> {
        template <static_str K>
        struct filter {
            using type = get_intersection<I + 1, string_map<U, Ks...>, out...>::type;
            static constexpr type operator()(auto&& other, auto&&... args) noexcept(
                noexcept(get_intersection<I + 1, string_map<U, Ks...>, out...>{}(
                    std::forward<decltype(other)>(other),
                    std::forward<decltype(args)>(args)...
                ))
            ) {
                return get_intersection<I + 1, string_map<U, Ks...>, out...>{}(
                    std::forward<decltype(other)>(other),
                    std::forward<decltype(args)>(args)...
                );
            }
        };
        template <static_str K> requires (contains<K>())
        struct filter<K> {
            using type = get_intersection<I + 1, string_map<U, Ks...>, out..., K>::type;
            static constexpr type operator()(auto&& other, auto&&... args) noexcept(
                noexcept(get_intersection<I + 1, string_map<U, Ks...>, out..., K>{}(
                    std::forward<decltype(other)>(other),
                    std::forward<decltype(args)>(args)...,
                    std::forward<decltype(other)>(other).template get<K>()
                ))
            ) {
                return get_intersection<I + 1, string_map<U, Ks...>, out..., K>{}(
                    std::forward<decltype(other)>(other),
                    std::forward<decltype(args)>(args)...,
                    std::forward<decltype(other)>(other).template get<K>()
                );
            }
        };
        using type = filter<meta::unpack_string<I, Ks...>>::type;
        static constexpr type operator()(auto&& other, auto&&... args) noexcept(
            noexcept(filter<meta::unpack_string<I, Ks...>>{}(
                std::forward<decltype(other)>(other),
                std::forward<decltype(args)>(args)...
            ))
        ) {
            return filter<meta::unpack_string<I, Ks...>>{}(
                std::forward<decltype(other)>(other),
                std::forward<decltype(args)>(args)...
            );
        }
    };

    template <size_t I, typename Other, static_str... out>
    struct get_difference {
        using type = string_map<mapped_type, out...>;
        static constexpr type operator()(auto&& self, auto&&... args) noexcept(
            noexcept(type{std::forward<decltype(args)>(args)...})
        ) {
            return {std::forward<decltype(args)>(args)...};
        }
    };
    template <size_t I, typename Other, static_str... out>
        requires (I < size())
    struct get_difference<I, Other, out...> {
        template <static_str K>
        struct filter {
            using type = get_difference<I + 1, Other, out..., K>::type;
            static constexpr type operator()(auto&& self, auto&&... args) noexcept(
                noexcept(get_difference<I + 1, Other, out..., K>{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(args)>(args)...,
                    std::forward<decltype(self)>(self).template get<K>()
                ))
            ) {
                return get_difference<I + 1, Other, out..., K>{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(args)>(args)...,
                    std::forward<decltype(self)>(self).template get<K>()
                );
            }
        };
        template <static_str K> requires (Other::template contains<K>())
        struct filter<K> {
            using type = get_difference<I + 1, Other, out...>::type;
            static constexpr type operator()(auto&& self, auto&&... args) noexcept(
                noexcept(get_difference<I + 1, Other, out...>{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(args)>(args)...
                ))
            ) {
                return get_difference<I + 1, Other, out...>{}(
                    std::forward<decltype(self)>(self),
                    std::forward<decltype(args)>(args)...
                );
            }
        };
        using type = filter<meta::unpack_string<I, Keys...>>::type;
        static constexpr type operator()(auto&& self, auto&&... args) noexcept(
            noexcept(filter<meta::unpack_string<I, Keys...>>{}(
                std::forward<decltype(self)>(self),
                std::forward<decltype(args)>(args)...
            ))
        ) {
            return filter<meta::unpack_string<I, Keys...>>{}(
                std::forward<decltype(self)>(self),
                std::forward<decltype(args)>(args)...
            );
        }
    };

public:

    /* Get the union of two string maps.  Values from the other map will take priority
    in case of conflicts. */
    template <typename Self, meta::string_map Other>
        requires (meta::has_common_type<
            mapped_type,
            typename meta::unqualify<Other>::mapped_type
        >)
    [[nodiscard]] constexpr auto operator|(this Self&& self, Other&& other) noexcept(
        noexcept(get_union<0, 0, meta::unqualify<Other>>{}(
            std::forward<Self>(self),
            std::forward<Other>(other)
        ))
    ) {
        return get_union<0, 0, meta::unqualify<Other>>{}(
            std::forward<Self>(self),
            std::forward<Other>(other)
        );
    }

    /* Get the intersection of two string maps.  The result will only contain values
    from the other map. */
    template <meta::string_map Other>
    [[nodiscard]] constexpr auto operator&(Other&& other) const noexcept(
        noexcept(get_intersection<0, meta::unqualify<Other>>{}(std::forward<Other>(other)))
    ) {
        return get_intersection<0, meta::unqualify<Other>>{}(std::forward<Other>(other));
    }

    /* Get the difference of two string maps.  The result will only contains values
    from this map. */
    template <typename Self, meta::string_map Other>
    [[nodiscard]] constexpr auto operator-(this Self&& self, const Other& other) noexcept(
        noexcept(get_difference<0, meta::unqualify<Other>>{}(std::forward<Self>(self)))
    ) {
        return get_difference<0, meta::unqualify<Other>>{}(std::forward<Self>(self));
    }

    /* Get the symmetric difference of two string maps.  The result will contain only
    values from the  */
    template <typename Self, meta::string_map Other>
        requires (meta::has_common_type<
            mapped_type,
            typename meta::unqualify<Other>::mapped_type
        >)
    [[nodiscard]] constexpr auto operator^(this Self&& self, Other&& other) noexcept(
        noexcept(
            (std::forward<Self>(self) - std::forward<Other>(other)) |
            (std::forward<Other>(other) - std::forward<Self>(self))
        )
    ) {
        return
            (std::forward<Self>(self) - std::forward<Other>(other)) |
            (std::forward<Other>(other) - std::forward<Self>(self));
    }

    /* Check two string maps for equality.  This is true if all keys are present in
    both maps and have equivalent values, regardless of order. */
    template <typename U, static_str... OtherKeys>
    [[nodiscard]] constexpr bool operator==(
        const string_map<U, OtherKeys...>& other
    ) const noexcept(meta::nothrow::has_eq<const_mapped_ref, const_mapped_ref>) {
        if constexpr (size() == sizeof...(OtherKeys) && (contains<OtherKeys>() && ...)) {
            return ((get<OtherKeys>() == other.template get<OtherKeys>()) && ...);
        } else {
            return false;
        }
    }

    /* Check two string maps for inequality.  Equivalent to `!(map == other)`. */
    template <typename U, static_str... OtherKeys>
    [[nodiscard]] constexpr bool operator!=(
        const string_map<U, OtherKeys...>& other
    ) const noexcept(noexcept(*this == other)) {
        return !(*this == other);
    }

private:
    static constexpr std::array<size_t, size()> hash_index {(hasher{}(Keys) % size())...};

    static constexpr auto pack_index = []<size_t... Is>(std::index_sequence<Is...>) {
        std::array<size_t, size()> out;
        ((out[hash_index[Is]] = Is), ...);
        return out;
    }(std::make_index_sequence<size()>());

    using Table = std::array<value_type, size()>;
    Table table;

    template <size_t... Is>
    static constexpr auto build_table(
        std::index_sequence<Is...>,
        auto&&... values
    ) noexcept(
        noexcept(Table{value_type{
            meta::unpack_string<pack_index[Is], Keys...>,
            meta::unpack_arg<pack_index[Is]>(std::forward<decltype(values)>(values)...)
        }...})
    ) {
        return Table{value_type{
            meta::unpack_string<pack_index[Is], Keys...>,
            meta::unpack_arg<pack_index[Is]>(std::forward<decltype(values)>(values)...)
        }...};
    }

    template <size_t... Is>
    static constexpr auto copy_table(
        std::index_sequence<Is...>,
        const string_map& other
    ) noexcept(
        noexcept(Table{value_type{
            other.table[Is].first,
            other.table[Is].second
        }...})
    ) {
        return Table{value_type{
            other.table[Is].first,
            other.table[Is].second
        }...};
    }

    template <size_t... Is>
    static constexpr auto move_table(
        std::index_sequence<Is...>,
        string_map&& other
    ) noexcept(
        noexcept(Table{value_type{
            other.table[Is].first,
            std::forward<mapped_type>(other.table[Is].second)
        }...})
    ) {
        return Table{value_type{
            other.table[Is].first,
            std::forward<mapped_type>(other.table[Is].second)
        }...};
    }

    template <size_t... Is>
        requires (!meta::lvalue<mapped_type> && meta::copy_assignable<mapped_type>)
    constexpr void copy_assign_table(
        std::index_sequence<Is...>,
        const string_map& other
    ) noexcept(
        noexcept(((table[Is].second = other.table[Is].second), ...))
    ) {
        ((table[Is].second = other.table[Is].second), ...);
    }

    template <size_t I>
        requires (
            meta::destructible<value_type> &&
            meta::constructible_from<value_type, const key_type&, const_mapped_ref>
        )
    constexpr void copy(const string_map& other) noexcept(
        meta::nothrow::destructible<value_type> &&
        meta::nothrow::constructible_from<value_type, const key_type&, const_mapped_ref>
    ) {
        if constexpr (!meta::trivially_destructible<value_type>) {
            table[I].~value_type();
        }
        new (&table[I]) value_type(
            other.table[I].first,
            other.table[I].second
        );
    };

    template <size_t... Is>
    constexpr void copy_assign_table(
        std::index_sequence<Is...>,
        const string_map& other
    )
        noexcept(noexcept((copy<Is>(other), ...)))
        requires(requires{(copy<Is>(other), ...);})
    {
        (copy<Is>(other), ...);
    }

    template <size_t... Is>
        requires (!meta::lvalue<mapped_type> && meta::copy_assignable<mapped_type>)
    constexpr void move_assign_table(
        std::index_sequence<Is...>,
        string_map&& other
    ) noexcept(
        noexcept(((table[Is].second = std::forward<mapped_type>(other.table[Is].second)), ...))
    ) {
        ((table[Is].second = std::forward<mapped_type>(other.table[Is].second)), ...);
    }

    template <size_t I>
        requires (
            meta::destructible<value_type> &&
            meta::constructible_from<value_type, const key_type&, mapped_type>
        )
    constexpr void move(string_map&& other) noexcept(
        meta::nothrow::destructible<value_type> &&
        noexcept(new (&table[I]) value_type(
            other.table[I].first,
            std::forward<mapped_type>(other.table[I].second)
        ))
    ) {
        if constexpr (!meta::trivially_destructible<value_type>) {
            table[I].~value_type();
        }
        new (&table[I]) value_type(
            other.table[I].first,
            std::forward<mapped_type>(other.table[I].second)
        );
    }

    template <size_t... Is>
    constexpr void move_assign_table(
        std::index_sequence<Is...>,
        string_map&& other
    )
        noexcept(noexcept((move<Is>(std::move(other)), ...)))
        requires(requires{(move<Is>(std::move(other)), ...);})
    {
        (move<Is>(std::move(other)), ...);
    }

public:
    constexpr string_map()
        noexcept(meta::nothrow::default_constructible<mapped_type>)
        requires(meta::default_constructible<mapped_type>)
    {};

    template <typename... Values>
        requires (
            sizeof...(Values) == size() &&
            (meta::convertible_to<Values, mapped_type> && ...)
        )
    constexpr string_map(Values&&... values) noexcept(
        noexcept(build_table(
            std::make_index_sequence<size()>{},
            std::forward<Values>(values)...
        ))
    ) : table(build_table(
        std::make_index_sequence<size()>{},
        std::forward<Values>(values)...
    )) {}

    template <typename Self = string_map>
        requires(requires(Self& self, const Self& other) {
            copy_table(std::make_index_sequence<size()>{}, other);
        })
    constexpr string_map(const string_map& other) noexcept(
        noexcept(copy_table(std::make_index_sequence<size()>{}, other))
    ) : table(copy_table(
        std::make_index_sequence<size()>{},
        other
    )) {}

    template <typename Self = string_map>
        requires(requires(Self& self, Self&& other) {
            move_table(std::make_index_sequence<size()>{}, std::move(other));
        })
    constexpr string_map(string_map&& other) noexcept(
        noexcept(move_table(
            std::make_index_sequence<size()>{},
            std::move(other)
        ))
    ) : table(move_table(
        std::make_index_sequence<size()>{},
        std::move(other)
    )) {}

    template <typename Self = string_map>
        requires(requires(Self& self, const Self& other) {
            self.copy_assign_table(std::make_index_sequence<size()>{}, other);
        })
    constexpr string_map& operator=(const string_map& other) noexcept(
        noexcept(copy_assign_table(std::make_index_sequence<size()>{}, other))
    ) {
        if (this != &other) {
            copy_assign_table(std::make_index_sequence<size()>{}, other);
        }
        return *this;
    }

    template <typename Self = string_map>
        requires(requires(Self& self, Self&& other) {
            self.move_assign_table(
                std::make_index_sequence<size()>{},
                std::move(other)
            );
        })
    constexpr string_map& operator=(string_map&& other) noexcept(
        noexcept(move_assign_table(
            std::make_index_sequence<size()>{},
            std::move(other)
        ))
    ) {
        if (this != &other) {
            move_assign_table(std::make_index_sequence<size()>{}, std::move(other));
        }
        return *this;
    }
};


namespace impl {

    /* Visitor for `bertrand::repr()`. */
    template <typename Char>
    struct Repr {
        using type = std::basic_string<Char>;

        // 1) prefer std::format() if it is available
        template <typename T>
        [[nodiscard]] static constexpr type operator()(T&& obj)
            noexcept(noexcept(std::format("{}", std::forward<T>(obj))))
            requires(std::formattable<T, Char>)
        {
            return std::format(
                std::basic_format_string<Char, T>("{}"),
                std::forward<T>(obj)
            );
        }

        // 2) prefer explicit conversion to std::basic_string<Char>
        template <typename T>
        [[nodiscard]] static constexpr type operator()(T&& obj)
            noexcept(noexcept(static_cast<std::basic_string<Char>>(std::forward<T>(obj))))
            requires(
                !std::formattable<T, Char> &&
                meta::explicitly_convertible_to<T, type>
            )
        {
            return static_cast<type>(std::forward<T>(obj));
        }

        // 3) attempt stream insertion operator
        template <typename T>
        [[nodiscard]] static constexpr type operator()(T&& obj)
            noexcept(
                noexcept(std::basic_ostringstream<Char>{} << std::forward<T>(obj)) &&
                noexcept(std::basic_ostringstream<Char>{}.str())
            )
            requires(
                !std::formattable<T, Char> &&
                !meta::explicitly_convertible_to<T, type> &&
                meta::has_stream_insertion<T, Char>
            )
        {
            std::basic_ostringstream<Char> stream;
            stream << std::forward<T>(obj);
            return stream.str();
        }

        // 4) fall back to generic representation
        template <typename T>
        [[nodiscard]] static constexpr type operator()(const T& obj)
            noexcept(noexcept(
                "<" + demangle<T>() + " at " +
                std::to_string(reinterpret_cast<uintptr_t>(&obj)) + ">"
            ))
            requires(
                !std::formattable<T, Char> &&
                !meta::explicitly_convertible_to<T, type> &&
                !meta::has_stream_insertion<T, Char> &&
                meta::has_stream_insertion<uintptr_t, Char>
            )
        {
            static constexpr static_str prefix = "<" + demangle<T>() + " at ";
            std::basic_ostringstream<Char> stream;
            stream << prefix << reinterpret_cast<uintptr_t>(&obj) << ">";
            return stream.str();
        }
    };

}


/* Get a simple string representation of an arbitrary object.  This is functionally
equivalent to Python's `repr()` function, but extended to work for all C++ types.  It
will attempt the following, in order of precedence:

    1.  A `std::format()` call using a registered `std::formatter<>` specialization.
        Note that no options will be provided to the formatter in this case - use
        `bertrand::format()` to customize the output.
    2.  An explicit conversion to `std::basic_string<Char>`.
    3.  A stream insertion operator (`<<`) without any modifiers.
    4.  A generic identifier based on the demangled type name and memory address,
        similar to Python.

Additionally, note that this function automatically visits any union types that are
provided as arguments, so you can safely use it with `bertrand::Union`,
`bertrand::Optional`, and `bertrand::Expected`, as well as any of their STL
counterparts.

If you'd like to extend `repr()` for a specific type, the best way to is to implement a
`std::formatter<T, Char>` specialization for that type, which allows Python-style
format strings in C++, whose semantics will be reflected in Python itself. */
template <typename Char = char, typename T>
[[nodiscard]] constexpr std::basic_string<Char> repr(T&& obj)
    noexcept(meta::nothrow::visit<impl::Repr<Char>, T>)
    requires(meta::visit<impl::Repr<Char>, T>)
{
    return visit(impl::Repr<Char>{}, std::forward<T>(obj));
}


namespace impl {

    template <typename...>
    struct _format_repr;
    template <typename Char, typename... out, typename... Args>
    struct _format_repr<Char, meta::pack<out...>, Args...> {
        using type = std::basic_format_string<Char, out...>;
    };
    template <typename Char, typename... out, typename A, typename... Args>
    struct _format_repr<Char, meta::pack<out...>, A, Args...> {
        template <typename T>
        struct to_repr { using type = std::basic_string<Char>; };
        template <std::formattable<Char> T>
        struct to_repr<T> { using type = T; };
        using type = _format_repr<
            Char,
            meta::pack<out..., typename to_repr<A>::type>,
            Args...
        >::type;
    };

    template <typename Char, typename... Args>
    using format_repr = _format_repr<Char, meta::pack<>, Args...>::type;

    template <typename Char, std::formattable<Char> A>
    constexpr decltype(auto) to_format_repr(A&& arg) noexcept {
        return (std::forward<A>(arg));
    }

    template <typename Char, typename A> requires (!std::formattable<A, Char>)
    constexpr decltype(auto) to_format_repr(A&& arg)
        noexcept(noexcept(bertrand::repr(std::forward<A>(arg))))
    {
        return bertrand::repr(std::forward<A>(arg));
    }

}


/* Format a string similar to `std::format()`.  Unformattable objects will be passed
through `repr()` to obtain a universal representation. */
template <typename Char = char, typename... Args>
constexpr std::string format(impl::format_repr<Char, Args...> fmt, Args&&... args)
    noexcept(noexcept(std::format(
        fmt,
        impl::to_format_repr<Char>(std::forward<Args>(args))...
    )))
    requires(meta::visit<impl::Repr<Char>, Args...>)
{
    return std::format(fmt, impl::to_format_repr<Char>(std::forward<Args>(args))...);
}


/* Print a format string to an output buffer.  Does not append a newline character.
Unformattable objects will be passed through `repr()` to obtain a universal
representation. */
template <typename Char = char, typename... Args>
constexpr void print(impl::format_repr<Char, Args...> fmt, Args&&... args)
    noexcept(noexcept(std::print(
        fmt,
        impl::to_format_repr<Char>(std::forward<Args>(args))...
    )))
    requires(meta::visit<impl::Repr<Char>, Args...>)
{
    std::print(fmt, impl::to_format_repr<Char>(std::forward<Args>(args))...);
}


/* Print an arbitrary value to an output buffer by calling `repr()` on it. Does not
append a newline character.  Unformattable objects will be passed through `repr()` to
obtain a universal representation. */
template <typename Char = char, typename T>
constexpr void print(T&& obj)
    noexcept(noexcept(std::cout << repr<Char>(std::forward<T>(obj))))
    requires(meta::visit<impl::Repr<Char>, T>)
{
    std::cout << repr<Char>(std::forward<T>(obj));
}


/* Print a format string to an output buffer.  Appends a newline character to the
output.  Unformattable objects will be passed through `repr()` to obtain a universal
representation. */
template <typename Char = char, typename... Args>
constexpr void println(impl::format_repr<Char, Args...> fmt, Args&&... args)
    noexcept(noexcept(std::println(
        fmt,
        impl::to_format_repr<Char>(std::forward<Args>(args))...
    )))
    requires(meta::visit<impl::Repr<Char>, Args...>)
{
    std::println(fmt, impl::to_format_repr<Char>(std::forward<Args>(args))...);
}


/* Print an arbitrary value to an output buffer by calling `repr()` on it.  Appends a
newline character to the output.  Unformattable objects will be passed through `repr()`
to obtain a universal representation. */
template <typename Char = char, typename T>
constexpr void println(T&& obj)
    noexcept(noexcept(std::cout << repr<Char>(std::forward<T>(obj)) << "\n"))
    requires(meta::visit<impl::Repr<Char>, T>)
{
    std::cout << repr<Char>(std::forward<T>(obj)) << "\n";
}


/* A python-style `assert` statement in C++, which is only available in debug mode, and
throws an `AssertionError` rather than immediately terminating the program.

Attempting to call this function in a release build will result in a compilation
(static assert) failure, enforcing strict discipline at compile time.  Practically
speaking, this requires all assertions to be guarded as follows:

    ```
    if constexpr (DEBUG) {
        assert(cnd, "assertion failed: {}", cnd);
    }
    ```

Or be omitted entirely in release builds.

NOTE: Because this function throws an `AssertionError` that can be translated up to
Python as an equivalent exception, and bindings will be automatically generated for
all exported entities, it is possible to use this function to define pytest-style unit
tests directly in C++ that can be run from Python without any special handling.  For
example:

    ```
    export module my_tests;

    export void test_foo() requires (DEBUG) {
        assert(foo() == 42, "foo() should return 42");
    }
    ```

When the program is compiled, a Python module named `my_tests` will be created that
exposes the `test_foo()` function.  Pytest should be able to automatically discover
this function and run it as part of the test suite.  This is a powerful feature that
greatly simplifies the process of writing unit tests in C++ and allows for
seamless integration with existing Python test suites.

NOTE: Due to the spelling of this function, conflicts may arise with the C-style
`assert()` macro from `<cassert>`.  This was considered during the design of this
function, and the conflict was deemed acceptable for the following reasons:

    1.  C assertions are defined as macros, and thus cannot be exported from a module
        interface unit.
    2.  Because C assertions are macros, they do not permit commas in the assertion
        condition, which can cause problems when the condition is a template
        expression, which frequently contain commas.  This also prevents the assertion
        from being able to accept multiple arguments, which precludes the use of
        format strings in the assertion message (which must itself not contain commas).
    3.  Macros (including C assertions) do not respect namespace boundaries, and thus
        cannot be isolated from other code that may use the same name, regardless of
        scope.
    4.  On failure, C assertions will issue an abort signal, which can cause problems
        in a multi-threaded environment, since the signal will not be contained to the
        originating thread and cannot be caught by the user.  This also means that
        the assertion message cannot be passed up to Python, and will not retain a
        coherent stack trace to the point of failure.
    5.  An alternative spelling would add noise to the call site and break continuity
        with Python, which does not have this problem.  By keeping the spelling the
        same, it becomes much easier to parse the code and write pytest-style unit
        tests using this function, which is a significant advantage.

For these reasons, the C-style `assert()` macro is considered to be deprecated by
Bertrand, and should not be used in new code.  This function is intended to replace it
in all cases, and should always be preferred.  The only downside is that it is possible
for the C `assert()` macro to be reintroduced by a legacy header file, which may
include `<cassert>` somewhere in its dependency chain, thereby polluting the global
namespace.  This can be avoided by exclusively using C++20 modules to handle
dependencies (which Bertrand is set up to do by default) or manually undefining the
macro immediately before importing bertrand, like so:

    ```
    #include <legacy_header.h>

    #undef assert
    import bertrand;

    // bertrand::assert() is now available
    using bertrand::assert;  // if you'd like to alias the typical C-style assert()
    ```

If you fail to undefine the C assert() macro, then it will silently override this
function, regardless of namespace or scope.  This viral behavior is yet another reason
to deprecate the C-style `assert()` macro, and to avoid it at all costs. */
template <typename... Args>
[[gnu::always_inline]] void assert(
    bool cnd,
    impl::format_repr<char, Args...> msg = "",
    Args&&... args
) {
    static_assert(DEBUG, "assertions are only available in debug mode");
    if (!cnd) {
        throw AssertionError(std::format(
            msg,
            impl::to_format_repr<char>(std::forward<Args>(args))...
        ));
    }
}


/* A python-style `assert` statement in C++, which is only available in debug mode, and
throws an `AssertionError` rather than immediately terminating the program.

Attempting to call this function in a release build will result in a compilation
(static assert) failure, enforcing strict discipline at compile time.  Practically
speaking, this requires all assertions to be guarded as follows:

    ```
    if constexpr (DEBUG) {
        assert(cnd, "assertion failed: {}", cnd);
    }
    ```

Or be omitted entirely in release builds.

NOTE: Because this function throws an `AssertionError` that can be translated up to
Python as an equivalent exception, and bindings will be automatically generated for
all exported entities, it is possible to use this function to define pytest-style unit
tests directly in C++ that can be run from Python without any special handling.  For
example:

    ```
    export module my_tests;

    export void test_foo() requires (DEBUG) {
        assert(foo() == 42, "foo() should return 42");
    }
    ```

When the program is compiled, a Python module named `my_tests` will be created that
exposes the `test_foo()` function.  Pytest should be able to automatically discover
this function and run it as part of the test suite.  This is a powerful feature that
greatly simplifies the process of writing unit tests in C++ and allows for
seamless integration with existing Python test suites.

NOTE: Due to the spelling of this function, conflicts may arise with the C-style
`assert()` macro from `<cassert>`.  This was considered during the design of this
function, and the conflict was deemed acceptable for the following reasons:

    1.  C assertions are defined as macros, and thus cannot be exported from a module
        interface unit.
    2.  Because C assertions are macros, they do not permit commas in the assertion
        condition, which can cause problems when the condition is a template
        expression, which frequently contain commas.  This also prevents the assertion
        from being able to accept multiple arguments, which precludes the use of
        format strings in the assertion message (which must itself not contain commas).
    3.  Macros (including C assertions) do not respect namespace boundaries, and thus
        cannot be isolated from other code that may use the same name, regardless of
        scope.
    4.  On failure, C assertions will issue an abort signal, which can cause problems
        in a multi-threaded environment, since the signal will not be contained to the
        originating thread and cannot be caught by the user.  This also means that
        the assertion message cannot be passed up to Python, and will not retain a
        coherent stack trace to the point of failure.
    5.  An alternative spelling would add noise to the call site and break continuity
        with Python, which does not have this problem.  By keeping the spelling the
        same, it becomes much easier to parse the code and write pytest-style unit
        tests using this function, which is a significant advantage.

For these reasons, the C-style `assert()` macro is considered to be deprecated by
Bertrand, and should not be used in new code.  This function is intended to replace it
in all cases, and should always be preferred.  The only downside is that it is possible
for the C `assert()` macro to be reintroduced by a legacy header file, which may
include `<cassert>` somewhere in its dependency chain, thereby polluting the global
namespace.  This can be avoided by exclusively using C++20 modules to handle
dependencies (which Bertrand is set up to do by default) or manually undefining the
macro immediately before importing bertrand, like so:

    ```
    #include <legacy_header.h>

    #undef assert
    import bertrand;

    // bertrand::assert() is now available
    using bertrand::assert;  // if you'd like to alias the typical C-style assert()
    ```

If you fail to undefine the C assert() macro, then it will silently override this
function, regardless of namespace or scope.  This viral behavior is yet another reason
to deprecate the C-style `assert()` macro, and to avoid it at all costs. */
template <typename T>
[[gnu::always_inline]] void assert(bool cnd, T&& obj) {
    static_assert(DEBUG, "assertions are only available in debug mode");
    if (!cnd) {
        throw AssertionError(repr(std::forward<T>(obj)));
    }
}


}  // namespace bertrand


namespace std {

    template <bertrand::meta::static_str T>
    struct hash<T> {
        constexpr static size_t operator()(const T& str) {
            return bertrand::impl::fnv1a{}(
                str,
                bertrand::impl::fnv1a::seed,
                bertrand::impl::fnv1a::prime
            );
        }
    };

    template <bertrand::meta::static_str T>
    struct tuple_size<T> :
        std::integral_constant<size_t, std::remove_cvref_t<T>::size()>
    {};

    template <auto... Strings>
    struct tuple_size<bertrand::string_list<Strings...>> :
        std::integral_constant<size_t, bertrand::string_list<Strings...>::size()>
    {};

    template <auto... Keys>
    struct tuple_size<bertrand::string_set<Keys...>> :
        std::integral_constant<size_t, bertrand::string_set<Keys...>::size()>
    {};

    template <typename T, auto... Keys>
    struct tuple_size<bertrand::string_map<T, Keys...>> :
        std::integral_constant<size_t, bertrand::string_map<T, Keys...>::size()>
    {};

    template <size_t I, bertrand::meta::static_str T>
        requires (I < std::remove_cvref_t<T>::size())
    struct tuple_element<I, T> {
        using type = decltype(std::declval<T>().template get<I>());
    };

    template <size_t I, auto... Strings>
        requires (I < bertrand::string_list<Strings...>::size())
    struct tuple_element<I, bertrand::string_list<Strings...>> {
        using type = decltype(bertrand::string_list<Strings...>::template get<I>());
    };

    template <size_t I, auto... Keys>
        requires (I < bertrand::string_set<Keys...>::size())
    struct tuple_element<I, bertrand::string_set<Keys...>> {
        using type = decltype(bertrand::string_set<Keys...>::template get<I>());
    };

    template <size_t I, typename T, auto... Keys>
        requires (I < bertrand::string_map<T, Keys...>::size())
    struct tuple_element<I, bertrand::string_map<T, Keys...>> {
        using type = decltype(bertrand::string_map<T, Keys...>::template get<I>());
    };

    template <size_t I, bertrand::meta::static_str T>
        requires (I < std::tuple_size<T>::value)
    [[nodiscard]] constexpr decltype(auto) get(const T& str) noexcept {
        return str.template get<I>();
    }

    template <size_t I, auto... Strings>
        requires (I < bertrand::string_list<Strings...>::size())
    [[nodiscard]] constexpr decltype(auto) get(const bertrand::string_list<Strings...>& list)
        noexcept
    {
        return list.template get<I>();
    }

    template <size_t I, auto... Keys>
        requires (I < bertrand::string_set<Keys...>::size())
    [[nodiscard]] constexpr decltype(auto) get(const bertrand::string_set<Keys...>& set)
        noexcept
    {
        return set.template get<I>();
    }

    template <size_t I, typename T, auto... Keys>
        requires (I < bertrand::string_map<T, Keys...>::size())
    [[nodiscard]] constexpr decltype(auto) get(const bertrand::string_map<T, Keys...>& map)
        noexcept
    {
        return map.template get<I>();
    }

    template <bertrand::slice s, bertrand::meta::static_str T>
    [[nodiscard]] constexpr decltype(auto) get(const T& str) noexcept {
        return str.template get<s>();
    }

    template <bertrand::slice s, auto... Strings>
    [[nodiscard]] constexpr decltype(auto) get(const bertrand::string_list<Strings...>& list)
        noexcept
    {
        return list.template get<s>();
    }

    template <bertrand::slice s, auto... Keys>
    [[nodiscard]] constexpr decltype(auto) get(const bertrand::string_set<Keys...>& set)
        noexcept
    {
        return set.template get<s>();
    }

    template <bertrand::slice s, typename T, auto... Keys>
    [[nodiscard]] constexpr decltype(auto) get(const bertrand::string_map<T, Keys...>& map)
        noexcept
    {
        return map.template get<s>();
    }

    template <bertrand::static_str Key, auto... Keys>
    [[nodiscard]] constexpr decltype(auto) get(const bertrand::string_list<Keys...>& set)
        noexcept
    {
        return set.template get<Key>();
    }

    template <bertrand::static_str Key, typename T, auto... Keys>
    [[nodiscard]] constexpr decltype(auto) get(const bertrand::string_map<T, Keys...>& map)
        noexcept
    {
        return map.template get<Key>();
    }

}


// namespace bertrand {

//     inline void test() {
//         static constexpr static_str str = "Hello, world!";
//         static constexpr static_str str2 = "Hello, world!";
//         static constexpr const char* str3 = "Hello, world!";
//         static constexpr std::string str4 = "Hello, world!";
//         static constexpr std::string_view str5 = "Hello, world!";
//         static_assert(string_wrapper<str>::index<"w">() == 7);

//         static constexpr string_list<"foo", "bar", "baz"> list;
//         static_assert(list.index("bar").value() == 1);

//         static_assert(meta::has_eq<decltype(str5), decltype(str)>);

        
//         static_assert(str2 <= str5);
//         if (str3 == str) {
//             println("Hello, world!");
//         }


//         static constexpr string_set<"foo", "bar", "baz"> set;
//         static_assert(set.index("bar").value() == 1);

//         if constexpr (DEBUG) {
//             assert(str == str2, "str == str2");
//         }
//     }


// }


#endif  // BERTRAND_STATIC_STRING_H
