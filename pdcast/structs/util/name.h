// include guard: BERTRAND_STRUCTS_UTIL_NAME_H
#ifndef BERTRAND_STRUCTS_UTIL_NAME_H
#define BERTRAND_STRUCTS_UTIL_NAME_H

#include <array>  // std::array
#include <cstdint>  // uint32_t
#include <string_view>  // std::string_view
#include "string.h"  // String<>


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


namespace bertrand {
namespace structs {
namespace util {


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



}  // namespace util
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_NAME_H
