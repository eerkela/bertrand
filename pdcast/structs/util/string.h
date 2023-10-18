// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_UTIL_STRING_H
#define BERTRAND_STRUCTS_UTIL_STRING_H

#include <array>  // std::array
#include <cstdint>  // uint32_t
#include <sstream>  // std::ostringstream
#include <string>  // std::string
#include <string_view>  // std::string_view
#include <typeinfo>  // typeid()
#include <Python.h>  // CPython API
#include "except.h"  // catch_python()


/* NOTE: This file contains utilities for working with C++ and Python strings for
 * debugging, error reporting, and compile-time naming for Python objects.
 */


namespace bertrand {
namespace structs {
namespace util {


///////////////////////////////////////
////    COMPILE-TIME TYPE NAMES    ////
///////////////////////////////////////


/* Custom PyObjects require a dotted name to be used from Python.  Unfortunately,
 * accessing the name of a C++ type is not always straightforward, especially when
 * dealing with heavily templated types.  Moreover, since these types do not change
 * over the course of the program, we would prefer to compute them at compile-time
 * if possible.  There is no standard way to do this, but using some compiler-specific
 * trickery, we can do exactly that.
 *
 * In order to generate a Python-compatible name for a C++ type, we need to do the
 * following:
 *      - If the type defines a `static constexpr std::string_view type_name` attribute,
 *        then we can use that directly.  This allows users to configure custom names
 *        for their types, which will be reflected at the Python level.
 *      - Otherwise, we need to generate a name ourselves.  This is done by getting the
 *        mangled type name using compiler macros, and then sanitizing it to remove
 *        invalid characters.  This allows us to generate a name that is guaranteed to
 *        be unique for each type, but is still human-readable at the Python level.
 *              NOTE: automatic naming relies on compiler support and is not guaranteed
 *              to work across all platforms.  At minimum, it should be compatible with
 *              most popular compilers (including GCC, Clang, and MSVC), but the
 *              specific implementation may need to be tweaked over time as compiler
 *              standards evolve.
 *
 * Additionally, these strings may need to be concatenated to form a dotted name.  If
 * we're careful, we can do this at compile-time as well, which allows us to generate
 * the full dotted name for an arbitrary type and bake it into the final binary.  There
 * is thus no runtime overhead for using these names, and they can be passed up to
 * Python without any additional work.
 */


/* Get the Python-compatible name of the templated iterator, defaulting to the
mangled C++ type name. */
template <typename T, typename = void>
class TypeName {

/* Extract the fully qualified C++ name of the templated class.

    NOTE: name mangling is not standardized across compilers, so we have to use
    platform-specific macros here.  The */
    #if (defined(__GNUC__) && __GNUC__ >= 11) || (defined(__clang__) && __clang_major__ >= 16)
        /* GCC and Clang both support the __PRETTY_FUNCTION__ macro, which gives us
         * a string of the following form:
         *
         * constexpr std::string_view bertrand::structs::util::TypeName<T>::extract() [with T = <type>; ...]
         *
         * Where <type> is the name we want to extract.
         */

        /* Invoke the preprocessor macro to get a detailed function signature. */
        static constexpr const char* _signature() {
            return __PRETTY_FUNCTION__;
        }

        /* Extract the qualified name from the compiled function signature. */
        static constexpr std::string_view extract() {
            // Get the full signature of the function
            constexpr const char* sig = _signature();
            constexpr const char* prefix = "T = ";  // 4 characters

            // find prefix in the signature
            constexpr const char* token = find_substring(sig, prefix);
            static_assert(
                token != nullptr,
                "Could not extract type name from __PRETTY_FUNCTION__"
            );
            constexpr const char* start = token + 4;  // get next token after prefix

            // Find end of the type name (next semicolon or end of string)
            const char* end = start;
            while (*end != ';' && *end != ']' && *end != '\0') {
                ++end;
            }

            // return as constexpr string_view
            return std::string_view(start, end - start);
        }
    #elif defined(_MSC_VER)
        /* MSVC uses the __FUNCSIG__ macro, which gives us a string of the following
         * form:
         *
         * 
         */

        /* Invoke the preprocessor macro to get a detailed function signature. */
        static constexpr const char* _signature() { return __FUNCSIG__; }

        /* Extract the qualified name from the compiled function signature. */
        static constexpr std::string_view extract() { return __FUNCSIG__; }
    #else
        /* Otherwise, automatic naming is not supported.  In this case, we will have to
         * manually name each object using the `type_name` attribute described above.
         */
        static_assert(
            false,
            "Automatic naming is only supported in GCC (>= 11.0.0) and clang "
            "(>= 15.0.0)-based compilers - please define a `static constexpr "
            "std::string_view type_name` attribute for this type to generate "
            "Python-compatible names."
        );
    #endif

    /* Concatenate a sequence of std::string_views at compile-time. */
    template <const std::string_view&... Strings>
    class concat {
        /* Join all strings into a single std::array of chars with static storage. */
        static constexpr auto array = [] {
            // Get array with size equal to total length of strings 
            constexpr size_t len = (Strings.size() + ... + 0);
            std::array<char, len + 1> array{};

            // Append each string to the array
            auto append = [i = 0, &array](const auto& s) mutable {
                for (auto c : s) array[i++] = c;
            };
            (append(Strings), ...);
            array[len] = 0;  // null-terminate
            return array;
        }();

    public:
        /* Get the concatenated string as a std::string_view. */
        static constexpr std::string_view value { array.data(), array.size() - 1 };
    };

    /* Find the first occurrence of a substring within a string at compile time. */
    static constexpr const char* find_substring(const char* str, const char* substr) {
        while (*str) {
            const char* s = str;
            const char* sub = substr;
            while (*s == *sub && *sub) {
                ++s;
                ++sub;
            }
            if (!(*sub)) {
                return str;
            }
            ++str;
        }
        return nullptr;
    }

    /* Compute a hash for a compile-time string using FNV-1a to ensure uniqueness. */
    static constexpr uint32_t fnv1a(const char* str) {
        uint32_t hash = 2166136261u;  // common 32-bit basis for FNV-1a
        while (*str) {
            hash ^= static_cast<unsigned char>(*str++);
            hash *= 16777619u;  // common prime for 32-bit FNV-1a
        }
        return hash;
    }

    /* Get the raw name of the templated type. */
    static constexpr std::string_view raw_name = extract();

    /* Compute the hash of the literal C++ name to ensure uniqueness. */
    static constexpr uint32_t hash_value = fnv1a(raw_name.data());

    /* Convert hash to a character array with static storage duration. */
    static constexpr std::array<char, 11> hash_array = [](uint32_t hash) {
        std::array<char, 11> temp{};  // 10 digits (32-bit) + null-terminator
        for (int i = 9; i >= 0; --i) {
            temp[i] = '0' + (hash % 10);
            hash /= 10;
        }
        temp[10] = '\0';  // null-terminate
        return temp;
    }(hash_value);

    /* Interpret character array as std::string_view. */
    static constexpr std::string_view hash_str{hash_array.data()};

    /* Concatenate name and hash. */
    static constexpr std::string_view concatenated = concat<raw_name, hash_str>::value;

    /* Sanitize output, converting invalid characters into underscores.
    
    NOTE: because we previously appended a hash to the end of the type name, we can
    guarantee that sanitization will not produce an unintended name collision. */
    static constexpr std::array<char, concatenated.size() + 1> sanitized = [] {
        std::array<char, concatenated.size() + 1> sanitized{};  // null-terminated
        int j = 0;
        for (int i = 0; i < concatenated.size(); ++i) {
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

    /* dot for concatenating names into dotted Python paths. */
    static constexpr std::string_view dot = ".";

public:
    /* The original signature that was generated by the compiler, for debugging
    purposes. */
    static constexpr const char* signature = _signature();

    /* A unique, Python-compatible type name computed at compile time. */
    static constexpr std::string_view value{sanitized.data()};

    /* Extend this type's dotted name with another string. */
    template <const std::string_view& Other>
    static constexpr std::string_view extend = concat<value, dot, Other>::value;
};


/* Get the Python-compatible name of the templated iterator, using the static
`name` attribute if it is available. */
template <typename T>
struct TypeName<T, std::void_t<decltype(T::name)>> {
    static constexpr std::string_view value { T::name };
};


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
