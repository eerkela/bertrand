// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_UTIL_STRING_H
#define BERTRAND_STRUCTS_UTIL_STRING_H

#include <array>  // std::array
#include <cstdint>  // uint32_t
#include <sstream>  // std::ostringstream
#include <string>  // std::string
#include <string_view>  // std::string_view
#include <typeinfo>  // typeid()
#include <vector>  // std::vector
#include <Python.h>  // CPython API
#include "except.h"  // catch_python()


/* NOTE: This file contains utilities for working with C++ and Python strings for
 * debugging, error reporting, type naming, and compile-time string manipulations.
 */


namespace bertrand {
namespace structs {
namespace util {


///////////////////////////////////////////
////    COMPILE-TIME STRING METHODS    ////
///////////////////////////////////////////


/* Default string for compile-time `String<>` operations.

NOTE: in order to use `String<>` with the default value, an empty specialization must
be provided (i.e. `String<>::`, not `String::`). */
inline static constexpr std::string_view empty_string_view {"", 0};


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
prevents confusion between the two. */
template <const std::string_view& str = empty_string_view>
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

    static constexpr char offset_lower_to_upper = 'A' - 'a';
    static constexpr char offset_upper_to_lower = 'a' - 'A';

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
        return is_upper(c) ? c + offset_upper_to_lower : c;
    }

    /* Convert a character to upper case. */
    static constexpr char to_upper(char c) {
        return is_lower(c) ? c + offset_lower_to_upper : c;
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
        for (auto c : str) if (!is_alpha(c)) return false;
        return !str.empty();
    }();

    /* Check if all characters in the string are digits. */
    template <typename = void>
    static constexpr bool isdigit = [] {
        for (auto c : str) if (!is_digit(c)) return false;
        return !str.empty();
    }();

    /* Check if all characters in the string are alphanumeric. */
    template <typename = void>
    static constexpr bool isalnum = [] {
        for (auto c : str) if (!is_alnum(c)) return false;
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
                    if (is_lower(c)) return false;
                    expect_capital = false;
                } else {
                    if (is_upper(c)) return false;
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
        for (auto c : str) if (!is_space(c)) return false;
        return !str.empty();
    }();

    /* Check if all characters in the string are in the ASCII character set. */
    template <typename = void>
    static constexpr bool isascii = [] {
        for (auto c : str) if (!is_ascii(c)) return false;
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
            if (has_sign) array[i++] = str[0];
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


public:

    // TODO: strip, lstrip, rstrip, removeprefix, removesuffix

    ////////////////////////////
    ////    FIND/REPLACE    ////
    ////////////////////////////

private:


public:

    /* Count the total number of occurrences of a substring within the templated
    string. */
    template <const std::string_view& sub, size_t start = 0, size_t end = str.size()>
    static constexpr size_t count = [] {
        size_t total = 0;
        for (size_t i = start; i < end; ++i) {
            if (str.substr(i, sub.size()) == sub) {
                ++total;
                i += sub.size() - 1;
            }
        }
        return total;
    }();

    // TODO: find, index, startswith, endswith, replace, expandtabs

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
    template <const std::string_view& sep>
    class _split {

        /* Construct an array containing the start index and length of every substring
        in `str`. */
        template <size_t size>
        static constexpr auto bounds() {
            std::array<std::pair<size_t, size_t>, size + 1> arr{};
            size_t start = 0;
            size_t index = 0;

            // Find all occurrences of `sep` and record their boundaries in `parts`
            for (size_t i = 0; i < str.size() && index < size; ++i) {
                if (str.substr(i, sep.size()) == sep) {
                    size_t j = index++;
                    arr[j].first = start;
                    arr[j].second = i - start;
                    start = i + sep.size();
                    i += sep.size() - 1;
                }
            }
            arr[size].first = start;
            arr[size].second = str.size() - start;
            return arr;
        }

        /* Convert an array of bounds into an array of substrings. */
        template <size_t... I>
        static constexpr auto extract(
            std::index_sequence<I...>,
            const std::array<std::pair<size_t, size_t>, sizeof...(I) + 1>& parts
        ) {
            std::array<std::string_view, sizeof...(I) + 1> arr{
                str.substr(parts[I].first, parts[I].second)...
            };
            arr[sizeof...(I)] = str.substr(parts[sizeof...(I)].first);
            return arr;
        }

    public:
        static constexpr auto value = [] {
            constexpr size_t size = count<sep>;
            if constexpr (size == 0) {
                return std::array<std::string_view, 1>{str};
            } else {
                return extract(std::make_index_sequence<size>(), bounds<size>());
            }
        }();

    };

public:

    /* Concatenate strings at compile time, inserting the templated string as a
    separator between each token. */
    template <const std::string_view&... strings>
    static constexpr std::string_view join = _join<strings...>::value;

    /* Split strings at compile time using the templated string as a separator. */
    template <const std::string_view& sep>
    static constexpr auto split = _split<sep>::value;

    // TODO: partition, rpartition, rsplit, splitlines

};


//////////////////////////////////////
////    AUTOMATIC PYTHON NAMES    ////
//////////////////////////////////////


/* Custom PyObjects require a dotted name to be used from Python.  Unfortunately,
 * accessing the name of a C++ type is not always straightforward, especially when
 * dealing with heavily templated types.  Moreover, since these types do not change
 * over the course of the program, we would prefer to compute them at compile-time
 * if possible.  There is no standard way to do this, but using some compiler-specific
 * trickery, we can do exactly that.
 *
 * In order to generate a Python-compatible name for a C++ type, we need to do the
 * following:
 *      - If a specific specialization of `PyName` exists for a given type, then we
 *        can use that directly.  This makes it straightforward to assign a custom name
 *        to an arbitrary type, without requiring direct access to the type itself,
 *        thereby promoting the open/closed principle of reusable software.
 *      - Otherwise, we need to generate a name ourselves.  This is done by getting the
 *        raw type name using compiler macros, and then mangling it with a unique hash
 *        and sanitizing it to remove invalid characters.  This allows us to generate a
 *        name that is guaranteed to be unique for each type, but is still reasonably
 *        readable at the Python level.
 *              NOTE: automatic naming relies on compiler support and is not guaranteed
 *              to work identically across all platforms.  It should be compatible with
 *              the vast majority of popular compilers (including GCC, Clang, and
 *              MSVC-based solutions), but the specific implementation may need to be
 *              tweaked over time as compiler standards evolve.
 *
 * Additionally, these strings may need to be concatenated to form a dotted name.  The
 * `String` class provides a mechanism to do exactly that, as well as a number of other
 * useful compile-time string manipulations.  The syntax for doing so is as follows:
 *
 *      static constexpr std::string_view dot{".", 1};
 *      constexpr std::string_view name = String<dot>::join<PyName<T>, PyName<U>, ...>;
 *
 * This mimics the syntax of Python's `str.join()` method, and operates in a similar
 * fashion.  Note the need to forward-declare the separator string as a constexpr
 * string_view with static storage duration.  This is necessary for the template
 * evaluation to work correctly, as string literals are not valid template arguments
 * until C++20.  If using a compiler that supports C++20 syntax, then this can be
 * simplified to:
 *
 *      constexpr std::string_view name = String<".">::join<PyName<T>, PyName<U>, ...>;
 */


/* Get the Python-compatible name of the templated iterator, defaulting to a mangled
C++ type name.

NOTE: name mangling is not standardized across compilers, so we have to use
platform-specific macros in the general case.  This is less than desirable, but as of
C++17, it is the only way to introspect a unique name for an arbitrary type at
compile-time.  If this fails, or if a different name is desired, users can provide their
own specializations of this class to inject a custom name.

Automatic naming is supported on the following compilers:
    - GCC (>= 11.0.0)
    - Clang (>= 15.0.0)
    - MSVC (>= 19.29.30038 in C++17 mode)
*/
template <typename T>
class TypeName {
private:

    /* Extract the fully qualified C++ name of the templated class. */
    #if defined(__GNUC__) || defined(__clang__)
        /* GCC and Clang both support the __PRETTY_FUNCTION__ macro, which gives us
         * a string of the following form:
         *
         * constexpr std::string_view bertrand::structs::util::Typename<T>::_signature() [with T = {type}; ...]
         *
         * Where {type} is the name we want to extract.
         */

        /* Invoke the preprocessor macro to get a detailed function signature. */
        static constexpr std::string_view _signature() {
            return std::string_view(__PRETTY_FUNCTION__);
        }

        /* Extract the qualified name from the compiled function signature. */
        static constexpr std::string_view extract() {
            constexpr std::string_view sig = _signature();
            constexpr size_t token = sig.find("T = ");  // 4 characters
            static_assert(
                token != sig.npos,
                "Could not extract type name from __PRETTY_FUNCTION__"
            );
            constexpr size_t start = token + 4;  // get next token after prefix
            constexpr size_t stop = sig.find_first_of(";]\0", start);
            return sig.substr(start, stop - start);
        }

    #elif defined(_MSC_VER)
        /* MSVC uses the __FUNCSIG__ macro, which gives us a string of the following
         * form:
         *
         * class std::basic_string_view<char,struct std::char_traits<char> > __cdecl TypeName<[struct/class ]{type}[ ]>::_signature(void)
         *
         * Where {type} is the name we want to extract, and components in [] are
         * optional based on whether the type is a struct, class, or builtin type.
         */

        /* Invoke the preprocessor macro to get a detailed function signature. */
        static constexpr const char* _signature() { return __FUNCSIG__; }

        /* Extract the qualified name from the compiled function signature. */
        static constexpr std::string_view extract() {
            constexpr std::string_view sig = _signature();
            constexpr size_t token = sig.find("TypeName<");  // 9 characters
            static_assert(
                token != sig.npos,
                "Could not extract type name from __FUNCSIG__"
                );
            constexpr size_t start = token + 9;  // get next token after prefix
            constexpr size_t stop = sig.find(">::_signature(void)", start);
            constexpr std::string_view raw = sig.substr(start, stop - start);
            constexpr std::string_view result = (
                raw.rfind(' ') == raw.size() ?
                    raw.substr(0, raw.size() - 1) :
                    raw
            );
            if (result.find("struct ") == 0) {
                return result.substr(7, result.size() - 7);
            } else if (result.find("class ") == 0) {
                return result.substr(6, result.size() - 6);
            }
            return result;
        }

    #else
        /* Otherwise, automatic naming is not supported.  In this case, an explicit
         * specialization of this class must be provided to generate a compatible name.
         */
        static_assert(
            false,
            "Automatic naming is only supported in GCC (>= 11.0.0) and clang "
            "(>= 15.0.0)-based compilers - please define a specialization of "
            "`bertrand::structs::util::PyName` to generate Python-compatible names "
            "for this type."
        );

    #endif

public:
    /* Full signature that was returned by compiler. */
    static constexpr std::string_view signature = _signature();

    /* Extracted class name. */
    static constexpr std::string_view name = extract();

    /* Hash of class name to ensure uniqueness. */
    static constexpr uint32_t hash = [] {
        uint32_t val = 2166136261u;  // common 32-bit basis for FNV-1a
        const char* str = name.data();
        while (*str) {
            val ^= static_cast<unsigned char>(*str++);
            val *= 16777619u;  // common prime for 32-bit FNV-1a
        }
        return val;
    }();

private:

    /* Convert hash to a character array with static storage duration. */
    static constexpr std::array<char, 11> hash_array = [] {
        uint32_t val = hash;
        std::array<char, 11> arr{};  // 10 digits (32-bits) + null-terminator
        for (int i = 9; i >= 0; --i) {
            arr[i] = '0' + (val % 10);
            val /= 10;
        }
        arr[10] = '\0';  // null-terminate
        return arr;
    }();

    /* Interpret character array as std::string_view. */
    static constexpr std::string_view hash_str{hash_array.data(), hash_array.size()};

    /* Concatenate name and hash. */
    static constexpr std::string_view concatenated = String<>::join<name, hash_str>;

    /* Sanitize output, converting invalid characters into underscores. */
    static constexpr std::array<char, concatenated.size() + 1> sanitized = [] {
        std::array<char, concatenated.size() + 1> sanitized{};  // null-terminated
        int j = 0;
        for (size_t i = 0; i < concatenated.size(); ++i) {
            char c = concatenated[i];
            if ((c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9')
            ) {
                sanitized[j++] = c;
            } else {
                sanitized[j++] = '_';
            }
        }
        sanitized[concatenated.size()] = '\0';  // null-terminate
        return sanitized;
    }();

public:
    /* A unique, Python-compatible type name computed at compile time. */
    static constexpr std::string_view mangled{sanitized.data(), concatenated.size()};
};


/* Alias for TypeName<T>::mangled.  This can be easily overloaded to assign a custom
name to a particular type. */
template <typename T>
constexpr std::string_view PyName = TypeName<T>::mangled;


////////////////////////////////
////    UNIVERSAL REPR()    ////
////////////////////////////////


/* Python's repr() function is extremely useful for debugging, but there is no direct
 * equivalent for C++ objects.  This makes debugging C++ code more difficult,
 * especially when working with objects of unknown type (doubly so when Python objects
 * might be mixed in with static C++ types).
 *
 * This function attempts to solve that problem by providing a single, overloadable
 * template function that uses specialization and SFINAE to determine the best way to
 * stringify an arbitrary object.  This allows us to use the same interface for all
 * objects (whether Python or C++), and to easily extend the functionality to new types
 * as needed.  At the moment, this can accept any object that is:
 *      - convertible to PyObject*, in which case `PyObject_Repr()` is used.
 *      - convertible to std::string, in which case `std::to_string()` is used.
 *      - streamable into a std::ostringstream, in which case `operator<<` is used.
 *      - iterable, in which case each element is recursively unpacked according to the
 *        same rules as listed here.
 *      - none of the above, in which case the raw type name is returned using
 *        `typeid().name()`.
 */


/* A trait that determines which specialization of repr() is appropriate for a
given type. */
template <typename T>
class ReprTraits {
    using True = std::true_type;
    using False = std::false_type;
    using Stream = std::ostringstream;

    enum class Strategy {
        python,
        to_string,
        stream,
        iterable,
        type_id
    };

    /* Check if the templated type is a Python object. */
    template<typename U>
    static auto _python(U u) -> decltype(
        PyObject_Repr(std::forward<U>(u)), True{}
    );
    static auto _python(...) -> False;

    /* Check if the templated type is a valid input to std::to_string. */
    template<typename U>
    static auto _to_string(U u) -> decltype(
        std::to_string(std::forward<U>(u)), True{}
    );
    static auto _to_string(...) -> False;

    /* Check if the templated type supports std::ostringstream insertion. */
    template<typename U>
    static auto _streamable(U u) -> decltype(
        std::declval<Stream&>() << std::forward<U>(u), True{}
    );
    static auto _streamable(...) -> False;

    /* Check if the templated type is iterable. */
    template<typename U>
    static auto _iterable(U u) -> decltype(
        std::begin(std::forward<U>(u)), std::end(std::forward<U>(u)), True{}
    );
    static auto _iterable(...) -> False;

    /* Determine the Repr() overload to use for objects of the templated type. */
    static constexpr Strategy category = [] {
        if constexpr (decltype(_python(std::declval<T>()))::value) {
            return Strategy::python;
        } else if constexpr (decltype(_to_string(std::declval<T>()))::value) {
            return Strategy::to_string;
        } else if constexpr (decltype(_streamable(std::declval<T>()))::value) {
            return Strategy::stream;
        } else if constexpr (decltype(_iterable(std::declval<T>()))::value) {
            return Strategy::iterable;
        } else {
            return Strategy::type_id;
        }
    }();

public:
    static constexpr bool python = (category == Strategy::python);
    static constexpr bool streamable = (category == Strategy::stream);
    static constexpr bool to_string = (category == Strategy::to_string);
    static constexpr bool iterable = (category == Strategy::iterable);
    static constexpr bool type_id = (category == Strategy::type_id);
};


/* Get a string representation of a Python object using PyObject_Repr(). */
template <typename T, std::enable_if_t<ReprTraits<T>::python, int> = 0>
std::string repr(const T& obj) {
    if (obj == nullptr) {
        return std::string("NULL");
    }
    PyObject* py_repr = PyObject_Repr(obj);
    if (py_repr == nullptr) {
        throw catch_python<std::runtime_error>();
    }
    const char* c_repr = PyUnicode_AsUTF8(py_repr);
    if (c_repr == nullptr) {
        throw catch_python<std::runtime_error>();
    }
    Py_DECREF(py_repr);
    return std::string(c_repr);
}


/* Get a string representation of a C++ object using `std::to_string()`. */
template <typename T, std::enable_if_t<ReprTraits<T>::to_string, int> = 0>
std::string repr(const T& obj) {
    return std::to_string(obj);
}


/* Get a string representation of a C++ object by streaming it into a
`std::ostringstream`. */
template <typename T, std::enable_if_t<ReprTraits<T>::streamable, int> = 0>
std::string repr(const T& obj) {
    std::ostringstream stream;
    stream << obj;
    return stream.str();
}


/* Get a string representation of an iterable C++ object by recursively unpacking
it. */
template <typename T, std::enable_if_t<ReprTraits<T>::iterable, int> = 0>
std::string repr(const T& obj) {
    std::ostringstream stream;
    stream << '[';
    for (auto iter = std::begin(obj); iter != std::end(obj);) {
        stream << repr(*iter);
        if (++iter != std::end(obj)) {
            stream << ", ";
        }
    }
    stream << ']';
    return stream.str();
}


/* Get a string representation of an arbitrary C++ object by getting its mangled type
name.  NOTE: this is the default implementation if no specialization can be found. */
template <typename T, std::enable_if_t<ReprTraits<T>::type_id, int> = 0>
std::string repr(const T& obj) {
    return std::string(typeid(obj).name());
}


}  // namespace util
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_CORE_UTIL_H
