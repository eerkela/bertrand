#ifndef BERTRAND_STRUCTS_UTIL_STRING_H
#define BERTRAND_STRUCTS_UTIL_STRING_H

#include <array>  // std::array
#include <cstddef>  // size_t
#include <limits>  // std::numeric_limits
#include <string_view>  // std::string_view
#include <tuple>  // std::tuple
#include <utility>  // std::pair, std::index_sequence, std::make_index_sequence


/* NOTE: This file contains utilities for working with C++ and Python strings for
 * debugging, error reporting, type naming, and compile-time string manipulations.
 */


namespace bertrand {
namespace util {


///////////////////////////////////////////
////    COMPILE-TIME STRING METHODS    ////
///////////////////////////////////////////


/* Default string for compile-time `String<>` operations.

NOTE: in order to use `String<>` with the default value, an empty specialization must
be provided (i.e. `String<>::`, not `String::`). */
inline static constexpr std::string_view EMPTY_STRING_VIEW {"", 0};


/* Compile-time string manipulations.

NOTE: using this class requires a shift in thinking from runtime string manipulation.
First of all, the class is purely static, and cannot be instantiated anywhere in code.
Second, its methods are implemented as static constexpr members, which means that they
do not need to be invoked at runtime.  They accept arguments via constexpr template
parameters, which may need to be forward declared before use.  For example, consider
the following:

    static constexpr std::string_view foo{"foo"};
    static constexpr std::string_view bar{"bar"};
    static constexpr std::string_view sep{"."};
    std::cout << String<sep>::join<foo, bar>;  // yields "foo.bar"

This is functionally similar to the equivalent Python `".".join(["foo", "bar"])`, but
evaluated entirely at compile-time.  The use of angle brackets to invoke the join
method clearly differentiates these operations from their runtime counterparts, and
prevents confusion between the two.

Operations that do not accept any arguments can be invoked by providing empty angle
brackets, similar to an empty function signature.  For example:

    std::cout<< String<String<sep>::join<foo, bar>>::upper<>;  // yields "FOO.BAR"
*/
template <const std::string_view& str = EMPTY_STRING_VIEW>
class String {
public:

    /* Disallow instances of this class. */
    String() = delete;
    String(const String&) = delete;
    String(String&&) = delete;

    //////////////////////
    ////    CASING    ////
    //////////////////////

private:

    static constexpr char OFFSET_LOWER_TO_UPPER = 'A' - 'a';
    static constexpr char OFFSET_UPPER_TO_LOWER = 'a' - 'A';

    /* Check if a character is lower case. */
    static constexpr bool is_lower(char c) {
        return (c >= 'a' && c <= 'z');
    }

    /* Check if a character is upper case. */
    static constexpr bool is_upper(char c) {
        return (c >= 'A' && c <= 'Z');
    }

    /* Check if a character is a letter. */
    static constexpr bool is_alpha(char c) {
        return is_lower(c) || is_upper(c);
    }

    /* Check if a character is a digit. */
    static constexpr bool is_digit(char c) {
        return (c >= '0' && c <= '9');
    }

    /* Check if a character is alphanumeric. */
    static constexpr bool is_alnum(char c) {
        return is_alpha(c) || is_digit(c);
    }

    /* Check if a character is in the ASCII character set. */
    static constexpr bool is_ascii(char c) {
        return (c >= 0 && c <= 127);
    }

    /* Check if a character is a whitespace character. */
    static constexpr bool is_space(char c) {
        switch (c) {
            case ' ':
            case '\t':
            case '\n':
            case '\r':
            case '\f':
            case '\v':
                return true;
            default:
                return false;
        }
    }

    /* Check if a character delimits a word boundary. */
    static constexpr bool is_delimeter(char c) {
        /* Switch statement compiles to a jump table, which is O(1). */
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

    /* Convert a character to lower case. */
    static constexpr char to_lower(char c) {
        return is_upper(c) ? c + OFFSET_UPPER_TO_LOWER : c;
    }

    /* Convert a character to upper case. */
    static constexpr char to_upper(char c) {
        return is_lower(c) ? c + OFFSET_LOWER_TO_UPPER : c;
    }

    /* Helper for converting a string to lower case. */
    template <typename = void>
    struct _lower {
        static constexpr std::array<char, str.size() + 1> array = [] {
            std::array<char, str.size() + 1> array{};  // null-terminated
            for (size_t i = 0; i < str.size(); ++i) {
                array[i] = to_lower(str[i]);
            }
            array[str.size()] = '\0';  // null-terminate
            return array;
        }();

        static constexpr std::string_view value{array.data(), array.size() - 1};
    };

    /* Helper for converting a string to upper case. */
    template <typename = void>
    struct _upper {
        static constexpr std::array<char, str.size() + 1> array = [] {
            std::array<char, str.size() + 1> array{};  // null-terminated
            for (size_t i = 0; i < str.size(); ++i) {
                array[i] = to_upper(str[i]);
            }
            array[str.size()] = '\0';  // null-terminate
            return array;
        }();

        static constexpr std::string_view value{array.data(), array.size() - 1};
    };

    /* Helper for converting a string to capital case. */
    template <typename = void>
    struct _capitalize {
        static constexpr std::array<char, str.size() + 1> array = [] {
            std::array<char, str.size() + 1> array{};  // null-terminated
            bool do_capitalize = true;
            for (size_t i = 0; i < str.size(); ++i) {
                char c = str[i];
                if (do_capitalize && is_alpha(c)) {
                    array[i] = to_upper(c);
                    do_capitalize = false;
                } else {
                    array[i] = to_lower(c);
                }
            }
            array[str.size()] = '\0';  // null-terminate
            return array;
        }();

        static constexpr std::string_view value{array.data(), array.size() - 1};
    };

    /* Helper for converting a string to title case. */
    template <typename = void>
    struct _title {
        static constexpr std::array<char, str.size() + 1> array = [] {
            std::array<char, str.size() + 1> array{};  // null-terminated
            bool do_capitalize = true;
            for (size_t i = 0; i < str.size(); ++i) {
                char c = str[i];
                if (do_capitalize && is_alpha(c)) {
                    array[i] = to_upper(c);
                    do_capitalize = false;
                } else if (is_delimeter(c)) {
                    array[i] = c;
                    do_capitalize = true;
                } else {
                    array[i] = to_lower(c);
                }
            }
            array[str.size()] = '\0';  // null-terminate
            return array;
        }();

        static constexpr std::string_view value{array.data(), array.size() - 1};
    };

    /* Helper for swapping the case of every letter in a string. */
    template <typename = void>
    struct _swapcase {
        static constexpr std::array<char, str.size() + 1> array = [] {
            std::array<char, str.size() + 1> array{};  // null-terminated
            for (size_t i = 0; i < str.size(); ++i) {
                char c = str[i];
                array[i] = is_lower(c) ? to_upper(c) : to_lower(c);
            }
            array[str.size()] = '\0';  // null-terminate
            return array;
        }();

        static constexpr std::string_view value{array.data(), array.size() - 1};
    };

public:

    /* Check if all characters in the string are alphabetic. */
    template <typename = void>
    static constexpr bool isalpha = [] {
        for (auto c : str) {
            if (!is_alpha(c)) {
                return false;
            }
        }
        return !str.empty();
    }();

    /* Check if all characters in the string are digits. */
    template <typename = void>
    static constexpr bool isdigit = [] {
        for (auto c : str) {
            if (!is_digit(c)) {
                return false;
            }
        }
        return !str.empty();
    }();

    /* Check if all characters in the string are alphanumeric. */
    template <typename = void>
    static constexpr bool isalnum = [] {
        for (auto c : str) {
            if (!is_alnum(c)) {
                return false;
            }
        }
        return !str.empty();
    }();

    /* Check if all cased characters in the string are in lower case and there is at
    least one cased character. */
    template <typename = void>
    static constexpr bool islower = [] {
        bool has_cased = false;
        for (auto c : str) {
            if (is_upper(c)) {
                return false;
            } else if (is_lower(c)) {
                has_cased = true;
            }
        }
        return has_cased;
    }();

    /* Check if all cased characters in the string are in upper case and there is at
    least one cased character. */
    template <typename = void>
    static constexpr bool isupper = [] {
        bool has_cased = false;
        for (auto c : str) {
            if (is_lower(c)) {
                return false;
            } else if (is_upper(c)) {
                has_cased = true;
            }
        }
        return has_cased;
    }();

    /* Check if all characters in the string are in title case and there is at least
    one cased character. */
    template <typename = void>
    static constexpr bool istitle = [] {
        bool expect_capital = true;
        for (size_t i = 0; i < str.size(); ++i) {
            char c = str[i];
            if (is_alpha(c)) {
                if (expect_capital) {
                    if (is_lower(c)) {
                        return false;
                    }
                    expect_capital = false;
                } else {
                    if (is_upper(c)) {
                        return false;
                    }
                }
            } else if (is_delimeter(c)) {
                expect_capital = true;
            }
        }
        return !str.empty();
    }();

    /* Check if all characters in the string are whitespace. */
    template <typename = void>
    static constexpr bool isspace = [] {
        for (auto c : str) {
            if (!is_space(c)) {
                return false;
            }
        }
        return !str.empty();
    }();

    /* Check if all characters in the string are in the ASCII character set. */
    template <typename = void>
    static constexpr bool isascii = [] {
        for (auto c : str) {
            if (!is_ascii(c)) {
                return false;
            }
        }
        return true;
    }();

    /* Change all characters within the templated string to lower case. */
    template <typename = void>
    static constexpr std::string_view lower = _lower<>::value;

    /* Change all characters within the templated string to upper case. */
    template <typename = void>
    static constexpr std::string_view upper = _upper<>::value;

    /* Uppercase the first letter in the templated string and lowercase all others. */
    template <typename = void>
    static constexpr std::string_view capitalize = _capitalize<>::value;

    /* Uppercase the first letter in every word of the templated string.

    NOTE: word boundaries are determined by the `is_delimeter()` function, and should
    be robust for english strings.  This may not be the case for other languages, as
    the tools for doing complex string manipulations are not available strictly at
    compile time. */
    template <typename = void>
    static constexpr std::string_view title = _title<>::value;

    /* Swap the case of every character in the templated string. */
    template <typename = void>
    static constexpr std::string_view swapcase = _swapcase<>::value;

    ///////////////////////
    ////    PADDING    ////
    ///////////////////////

private:

    /* Helper for left-justifying a string. */
    template <size_t width, char fill>
    struct _ljust {
        static constexpr std::array<char, width + 1> array = [] {
            std::array<char, width + 1> array{};  // null-terminated
            size_t i = 0;
            for (; i < str.size(); ++i) {
                array[i] = str[i];
            }
            for (; i < width; ++i) {
                array[i] = fill;
            }
            array[width] = '\0';  // null-terminate
            return array;
        }();

        static constexpr std::string_view value{array.data(), width};
    };

    /* Helper for right-justifying a string. */
    template <size_t width, char fill>
    struct _rjust {
        static constexpr std::array<char, width + 1> array = [] {
            std::array<char, width + 1> array{};  // null-terminated
            size_t i = 0;
            size_t offset = width - str.size();
            for (; i < offset; ++i) {
                array[i] = fill;
            }
            for (; i < width; ++i) {
                array[i] = str[i - offset];
            }
            array[width] = '\0';  // null-terminate
            return array;
        }();

        static constexpr std::string_view value{array.data(), width};
    };

    /* Helper for centering a string. */
    template <size_t width, char fill>
    struct _center {
        static constexpr std::array<char, width + 1> array = [] {
            std::array<char, width + 1> array{};  // null-terminated
            size_t i = 0;
            size_t offset = (width - str.size()) / 2;
            for (; i < offset; ++i) {
                array[i] = fill;
            }
            for (; i < offset + str.size(); ++i) {
                array[i] = str[i - offset];
            }
            for (; i < width; ++i) {
                array[i] = fill;
            }
            array[width] = '\0';  // null-terminate
            return array;
        }();

        static constexpr std::string_view value{array.data(), width};
    };

    /* Helper for zfilling a string. */
    template <size_t width>
    struct _zfill {
        static constexpr std::array<char, width + 1> array = [] {
            std::array<char, width + 1> array{};  // null-terminated
            size_t i = 0;
            bool has_sign = (str.size() > 0 && (str[0] == '-' || str[0] == '+'));
            if (has_sign) {
                array[i++] = str[0];
            }
            size_t offset = width - str.size();
            for (; i < offset + has_sign; ++i) {
                array[i] = '0';
            }
            for (; i < width; ++i) {
                array[i] = str[i - offset];
            }
            array[width] = '\0';  // null-terminate
            return array;
        }();

        static constexpr std::string_view value{array.data(), width};
    };

public:

    /* Pad the left side of the string with a given character until it reaches a
    specified width. */
    template <size_t width, const char fill = ' '>
    static constexpr std::string_view ljust = [] {
        if constexpr (width <= str.size()) {
            return str;
        } else {
            return _ljust<width, fill>::value;
        }
    }();

    /* Pad the right side of the string with a given character until it reaches a
    specified width. */
    template <size_t width, const char fill = ' '>
    static constexpr std::string_view rjust = [] {
        if constexpr (width <= str.size()) {
            return str;
        } else {
            return _rjust<width, fill>::value;
        }
    }();

    /* Pad both sides of the string with a given character until it reaches a specified
    width. */
    template <size_t width, const char fill = ' '>
    static constexpr std::string_view center = [] {
        if constexpr (width <= str.size()) {
            return str;
        } else {
            return _center<width, fill>::value;
        }
    }();

    /* Pad the left side of the string with zeros until it reaches a specified width.
    
    NOTE: if the first character of the string is a `+` or `-` sign, then the sign will
    be preserved and remain at the beginning of the string. */
    template <size_t width>
    static constexpr std::string_view zfill = [] {
        if constexpr (width <= str.size()) {
            return str;
        } else {
            return _zfill<width>::value;
        }
    }();

    /////////////////////
    ////    STRIP    ////
    /////////////////////

private:

    static constexpr std::string_view WHITESPACE {" \t\n\r\f\v"};

    /* Helper for finding the index of the first non-stripped character in a string. */
    static constexpr size_t first(const std::string_view& chars) {
        size_t i = 0;
        for (; i < str.size(); ++i) {
            if (chars.find(str[i]) == chars.npos) {
                break;
            }
        }
        return i;
    }

    /* Helper for finding the index of the last non-stripped character in a string. */
    static constexpr size_t last(const std::string_view& chars, size_t start) {
        size_t i = str.size();
        for (; i > start; --i) {
            if (chars.find(str[i - 1]) == chars.npos) {
                break;
            }
        }
        return i;
    };

public:

    /* Remove leading and trailing characters from the templated string. */
    template <const std::string_view& chars = WHITESPACE>
    static constexpr std::string_view strip = [] {
        size_t start = first(chars);
        size_t stop = last(chars, start);
        return str.substr(start, stop - start);
    }();

    /* Remove leading characters from the templated string. */
    template <const std::string_view& chars = WHITESPACE>
    static constexpr std::string_view lstrip = [] {
        size_t start = first(chars);
        return str.substr(start);
    }();

    /* Remove trailing characters from the templated string. */
    template <const std::string_view& chars = WHITESPACE>
    static constexpr std::string_view rstrip = [] {
        size_t stop = last(chars, 0);
        return str.substr(0, stop);
    }();

    /* Remove a prefix from the templated string if it is present. */
    template <const std::string_view& prefix>
    static constexpr std::string_view removeprefix = [] {
        if constexpr (str.substr(0, prefix.size()) == prefix) {
            return str.substr(prefix.size());
        } else {
            return str;
        }
    }();

    /* Remove a suffix from the templated string if it is present. */
    template <const std::string_view& suffix>
    static constexpr std::string_view removesuffix = [] {
        if constexpr (
            str.size() < suffix.size() ||
            str.substr(str.size() - suffix.size()) != suffix
        ) {
            return str;
        } else {
            return str.substr(0, str.size() - suffix.size());
        }
    }();

    ////////////////////////////
    ////    FIND/REPLACE    ////
    ////////////////////////////

    /* Find the first occurrence of a substring within the templated string. */
    template <const std::string_view& sub, size_t start = 0, size_t end = str.size()>
    static constexpr size_t find = [] {
        if (end < sub.size()) {
            return str.npos;
        }
        for (size_t i = start; i <= end - sub.size(); ++i) {
            if (str.substr(i, sub.size()) == sub) {
                return i;
            }
        }
        return str.npos;
    }();

    /* Find the last occurrence of a substring within the templated string. */
    template <const std::string_view& sub, size_t start = 0, size_t end = str.size()>
    static constexpr size_t rfind = [] {
        if (end < sub.size()) {
            return str.npos;
        }
        for (size_t i = end - sub.size() + 1; i > start; --i) {
            size_t j = i - 1;  // avoid underflow
            if (str.substr(j, sub.size()) == sub) {
                return j;
            }
        }
        return str.npos;
    }();

    /* Count the total number of occurrences of a substring within the templated
    string. */
    template <const std::string_view& sub, size_t start = 0, size_t end = str.size()>
    static constexpr size_t count = [] {
        size_t total = 0;
        for (size_t i = start; i <= end - sub.size(); ++i) {
            if (str.substr(i, sub.size()) == sub) {
                ++total;
                i += sub.size() - 1;  // skip ahead
            }
        }
        return total;
    }();

    /* Check if the templated string starts with a given sequence of characters. */
    template <const std::string_view& prefix>
    static constexpr bool startswith = [] {
        return str.substr(0, prefix.size()) == prefix;
    }();

    /* Check if the templated string ends with a given sequence of characters. */
    template <const std::string_view& suffix>
    static constexpr bool endswith = [] {
        if (str.size() < suffix.size()) {
            return false;
        }
        return str.substr(str.size() - suffix.size()) == suffix;
    }();

private:

    static constexpr size_t MAX_SIZE_T = std::numeric_limits<size_t>::max();

    /* Helper for replacing substrings within the templated string. */
    template <const std::string_view& key, const std::string_view& val, size_t max_count>
    struct _replace {
        /* Compute the total length of the resulting string. */
        static constexpr size_t size() {
            size_t total = (count<key> < max_count) ? count<key> : max_count;
            return str.size() - (total * key.size()) + (total * val.size());
        }

        static constexpr std::array<char, size() + 1> array = [] {
            std::array<char, size() + 1> array{};  // null-terminated
            size_t i = 0;  // current string index
            size_t idx = 0;  // current array index
            size_t replacements = 0;  // number of replacements made

            // Replace occurrences of `key` with `val`, up to `max_count`
            for (; i < str.size() && replacements < max_count; ++i) {
                if (str.substr(i, key.size()) == key) {
                    for (auto c : val) array[idx++] = c;
                    i += key.size() - 1;  // skip ahead
                    ++replacements;
                } else {
                    array[idx++] = str[i];
                }
            }

            // If `max_count` was reached, copy the rest of the string
            for (; i < str.size(); ++i) {
                array[idx++] = str[i];
            }

            array[size()] = '\0';  // null-terminate
            return array;
        }();

        static constexpr std::string_view value{array.data(), size()};
    };

    /* Helper for expanding tabs within the templated string. */
    template <size_t tabsize = 8>
    struct _expandtabs {
        static constexpr std::string_view key{"\t"};
        static constexpr std::array<char, tabsize + 1> val_array = [] {
            std::array<char, tabsize + 1> array{};  // null-terminated
            for (size_t i = 0; i < tabsize; ++i) {
                array[i] = ' ';
            }
            array[tabsize] = '\0';  // null-terminate
            return array;
        }();
        static constexpr std::string_view val{val_array.data(), tabsize};

        /* Delegate to _replace<>. */
        static constexpr std::string_view value = _replace<
            key,
            val,
            MAX_SIZE_T
        >::value;
    };

public:

    /* Replace all occurrences of a substring within the templated string. */
    template <
        const std::string_view& key,
        const std::string_view& val,
        size_t max_count = MAX_SIZE_T
    >
    static constexpr std::string_view replace = [] {
        if constexpr (key == val || max_count == 0) {
            return str;
        } else {
            return _replace<key, val, max_count>::value;
        }
    }();

    /* Replace tab characters with a given number of spaces. */
    template <size_t tabsize = 8>
    static constexpr std::string_view expandtabs = _expandtabs<tabsize>::value;

    //////////////////////////
    ////    JOIN/SPLIT    ////
    //////////////////////////

private:

    /* Recursive template specializations to interleave a `str` token in between each
    concatenated string. */
    template <const std::string_view&...>
    struct _join;
    template <const std::string_view& first, const std::string_view&... rest>
    struct _join<first, rest...> {
        /* Join all strings into a single std::array of chars with static storage. */
        static constexpr auto array = [] {
            // Get array with size equal to total length of strings 
            constexpr size_t len = first.size() + ((rest.size() + str.size()) + ... + 0);
            std::array<char, len + 1> array{};  // null-terminated
            size_t idx = 0;
            for (auto c : first) array[idx++] = c;  // insert first string

            // Recursively insert each string into array
            auto append = [&idx, &array](const auto& string) mutable {
                for (auto c : str) array[idx++] = c;
                for (auto c : string) array[idx++] = c;
            };
            (append(rest), ...);
            array[idx] = '\0';  // null-terminate
            return array;
        }();

        /* Wrap array as std::string_view. */
        static constexpr std::string_view value{array.data(), array.size() - 1};
    };
    template <const std::string_view& last>
    struct _join<last> {
        static constexpr std::string_view value = last;
    };

    /* Helper for splitting `str` using a given separator token. */
    template <const std::string_view& sep, size_t max_count>
    struct _split {

        /* Construct an array containing the start index and length of every substring
        in `str`. */
        template <size_t size>
        static constexpr auto bounds() {
            std::array<std::pair<size_t, size_t>, size + 1> arr{};
            size_t prev = 0;
            size_t index = 0;

            // Find all occurrences of `sep` and record their boundaries
            for (size_t i = 0; i <= str.size() - sep.size() && index < size; ++i) {
                if (str.substr(i, sep.size()) == sep) {
                    size_t j = index++;
                    arr[j].first = prev;
                    arr[j].second = i - prev;
                    prev = i + sep.size();
                    i += sep.size() - 1;  // skip ahead, accounting for loop increment
                }
            }
            arr[size].first = prev;
            arr[size].second = str.size() - prev;
            return arr;
        }

        /* Convert an array of bounds into an array of substrings. */
        template <size_t... I>
        static constexpr auto extract(
            const std::index_sequence<I...>,
            const std::array<std::pair<size_t, size_t>, sizeof...(I)>& parts
        ) {
            return std::array<std::string_view, sizeof...(I)> {
                str.substr(parts[I].first, parts[I].second)...
            };
        }

        static constexpr auto value = [] {
            constexpr size_t size = (max_count < count<sep>) ? max_count : count<sep>;
            if constexpr (size == 0) {
                return std::array<std::string_view, 1>{str};
            } else {
                return extract(
                    std::make_index_sequence<size + 1>(),
                    bounds<size>()
                );
            }
        }();

    };

    /* Helper for splitting `str` using a given separator token, starting from the
    right side of the string. */
    template <const std::string_view& sep, size_t max_count>
    struct _rsplit {

        /* Construct an array containing the start index and length of every substring
        in `str`. */
        template <size_t size>
        static constexpr auto bounds() {
            std::array<std::pair<size_t, size_t>, size + 1> arr{};
            size_t prev = str.size();
            size_t index = 0;

            // Find all occurrences of `sep` and record their boundaries
            for (size_t i = str.size() - sep.size(); i > 0 && index < size; --i) {
                size_t j = i - 1;  // avoid underflow
                if (str.substr(j, sep.size()) == sep) {
                    size_t k = index++;
                    size_t l = j + sep.size();
                    arr[k].first = l;
                    arr[k].second = prev - l;
                    prev = j;
                    i -= sep.size() - 1;  // skip ahead, accounting for loop decrement
                }
            }
            arr[size].first = 0;
            arr[size].second = prev;
            return arr;
        }

        /* Convert an array of bounds into an array of substrings. */
        template <size_t... I>
        static constexpr auto extract(
            const std::index_sequence<I...>,
            const std::array<std::pair<size_t, size_t>, sizeof...(I)>& parts
        ) {
            return std::array<std::string_view, sizeof...(I)> {
                str.substr(parts[I].first, parts[I].second)...
            };
        }

        static constexpr auto value = [] {
            constexpr size_t size = (max_count < count<sep>) ? max_count : count<sep>;
            if constexpr (size == 0) {
                return std::array<std::string_view, 1>{str};
            } else {
                return extract(
                    std::make_index_sequence<size + 1>(),
                    bounds<size>()
                );
            }
        }();

    };

    /* Helper for splitting `str` at line breaks. */
    template <bool keepends>
    struct _splitlines {

        /* Check whether a character represents a line break. */
        static constexpr bool is_line_break(const char c) {
            // NOTE: multi-character line breaks (\r\n) have to be checked separately 
            switch (c) {
                case '\n':      // newline
                case '\r':      // carriage return
                case '\v':      // vertical tab
                case '\f':      // form feed
                case '\x1c':    // file separator
                case '\x1d':    // group separator
                case '\x1e':    // record separator
                case '\x85':    // next line (C1 control code)
                    return true;
                default:
                    return false;
            }
        }

        /* Compute the total number of lines in the string. */
        static constexpr size_t line_count = [] {
            if constexpr (str.empty()) {
                return static_cast<size_t>(0);
            }
            size_t total = 1;
            for (size_t i = 0; i < str.size(); ++i) {
                if (str.substr(i, 2) == "\r\n") {
                    ++total;
                    ++i;  // skip ahead
                } else if (is_line_break(str[i])) {
                    ++total;
                }
            }
            return total;
        }();

        /* Construct an array containing the start index and length of every line in
        `str`. */
        static constexpr auto bounds() {
            std::array<std::pair<size_t, size_t>, line_count> arr{};
            size_t prev = 0;
            size_t index = 0;

            // Find all occurrences of line breaks and record their boundaries
            for (size_t i = 0; i < str.size() && index < line_count - 1; ++i) {
                if (str.substr(i, 2) == "\r\n") {
                    size_t j = index++;
                    arr[j].first = prev;
                    if constexpr (keepends) {
                        arr[j].second = i - prev + 2;
                    } else {
                        arr[j].second = i - prev;
                    }
                    prev = i + 2;
                    ++i;  // skip ahead
                } else if (is_line_break(str[i])) {
                    size_t j = index++;
                    arr[j].first = prev;
                    if constexpr (keepends) {
                        arr[j].second = i - prev + 1;
                    } else {
                        arr[j].second = i - prev;
                    }
                    prev = i + 1;
                }
            }
            arr[line_count - 1].first = prev;
            arr[line_count - 1].second = str.size() - prev;
            return arr;
        }

        /* Convert an array of bounds into an array of substrings. */
        template <size_t... I>
        static constexpr auto extract(
            const std::index_sequence<I...>,
            const std::array<std::pair<size_t, size_t>, sizeof...(I)>& parts
        ) {
            return std::array<std::string_view, sizeof...(I)> {
                str.substr(parts[I].first, parts[I].second)...
            };
        }

        static constexpr auto value = [] {
            if constexpr (line_count == 0) {
                return std::array<std::string_view, 0>{};
            } else {
                return extract(
                    std::make_index_sequence<line_count>(),
                    bounds()
                );
            }
        }();

    };

public:

    /* Concatenate strings, inserting the templated string as a separator between each
    token. */
    template <const std::string_view&... strings>
    static constexpr std::string_view join = _join<strings...>::value;

    /* Split the templated string into an array of substrings using the given
    separator.  If `max_count` is given, then only split up to the specified number of
    times, from left to right. */
    template <const std::string_view& sep, size_t max_count = MAX_SIZE_T>
    static constexpr auto split = [] {
        if constexpr (sep == EMPTY_STRING_VIEW) {
            static_assert(sep != EMPTY_STRING_VIEW, "cannot split on empty string");
        } else {
            return _split<sep, max_count>::value;
        }
    }();

    /* Split the templated string into an array of substrings using the given
    separator.  If `max_count` is given, then only split up to the specified number of
    times, from right to left. */
    template <const std::string_view& sep, size_t max_count = MAX_SIZE_T>
    static constexpr auto rsplit = [] {
        if constexpr (sep == EMPTY_STRING_VIEW) {
            static_assert(sep != EMPTY_STRING_VIEW, "cannot split on empty string");
        } else {
            return _rsplit<sep, max_count>::value;
        }
    }();

    /* Split the templated string at the first occurrence of a separator, returning
    a three tuple containing the substring before the separator, the separator itself,
    and the substring after the separator. */
    template <const std::string_view& sep>
    static constexpr auto partition = [] {
        constexpr size_t idx = find<sep>;
        if constexpr (idx == str.npos) {
            return std::make_tuple(str, "", "");
        } else {
            return std::make_tuple(
                str.substr(0, idx),
                sep,
                str.substr(idx + sep.size())
            );
        }
    }();

    /* Split the templated string at the last occurrence of a separator, returning
    a three tuple containing the substring before the separator, the separator itself,
    and the substring after the separator. */
    template <const std::string_view& sep>
    static constexpr auto rpartition = [] {
        constexpr size_t idx = rfind<sep>;
        if constexpr (idx == str.npos) {
            return std::make_tuple("", "", str);
        } else {
            return std::make_tuple(
                str.substr(0, idx),
                sep,
                str.substr(idx + sep.size())
            );
        }
    }();

    /* Split the templated string at line breaks, returning an array of substrings. */
    template <bool keepends = false>
    static constexpr auto splitlines = _splitlines<keepends>::value;

};


}  // namespace util
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_CORE_UTIL_H
