#ifndef BERTRAND_STRUCTS_UTIL_BASE_H
#define BERTRAND_STRUCTS_UTIL_BASE_H

#include <cstddef>  // size_t
#include <chrono>  // std::chrono::system_clock
#include <fstream>  // std::ofstream
#include <iomanip>  // std::put_time()
#include <ios>  // std::ios::app
#include <iostream>  // std::cerr
#include <string>  // std::string
#include <type_traits>  // std::is_pointer_v<>, std::is_convertible_v<>, etc.
#include <utility>  // std::pair, std::tuple
#include <Python.h>  // CPython API


namespace bertrand {


/* Check if a type is convertible to PyObject*. */
template <typename T>
inline constexpr bool is_pyobject = std::is_convertible_v<
    std::remove_cv_t<std::remove_reference_t<T>>,
    PyObject*
>;


/* Check if a type is identical to PyObject*. */
template <typename T>
inline constexpr bool is_pyobject_exact = std::is_same_v<
    std::remove_cv_t<std::remove_reference_t<T>>,
    PyObject*
>;


namespace util {

    template <typename T>
    struct is_pairlike : std::false_type {};

    template <typename X, typename Y>
    struct is_pairlike<std::pair<X, Y>> : std::true_type {};

    template <typename X, typename Y>
    struct is_pairlike<std::tuple<X, Y>> : std::true_type {};

}


/* Check if a C++ type is pair-like (i.e. a std::pair or std::tuple of size 2). */
template <typename T>
inline constexpr bool is_pairlike = util::is_pairlike<
    std::remove_cv_t<std::remove_reference_t<T>>
>::value;


template <typename T>
struct remove_rvalue {
    using type = T;
};


template <typename T>
struct remove_rvalue<T&&> {
    using type = T;
};


template <typename T>
using remove_rvalue_t = typename remove_rvalue<T>::type;


///////////////////////
////    LOGGING    ////
///////////////////////


/* DEBUG=true enables logging statements across the linked data structures, which will
be dumped to a .log file in the current working directory.  */

#ifdef BERTRAND_DEBUG
    inline constexpr bool DEBUG = true;
#else
    inline constexpr bool DEBUG = false;
#endif


/* Specialization for when logging is disabled.  This raises a compile-time error if
the logger is used without being guarded by an `if constexpr (DEBUG)` branch. */
template <bool Enable>
struct Logger {

    template <typename... Args>
    inline void operator()(Args&&... messages) {
        static_assert(Enable, "logging is not enabled.");
    }

    inline void indent() {
        static_assert(Enable, "logging is not enabled.");
    }

    inline void unindent() {
        static_assert(Enable, "logging is not enabled.");
    }

    inline void reset_indent() {
        static_assert(Enable, "logging is not enabled.");
    }

    inline void indent_level() const {
        static_assert(Enable, "logging is not enabled.");
    }

    inline void language(const std::string& lang) {
        static_assert(Enable, "logging is not enabled.");
    }

};


/* Specialization for when logging is enabled.  This dumps log statements to a .log
file in the current working directory.  If the data structures are used from Python,
then this will be the working directory of the Python interpreter itself, which is
usually the location from which the interpreter was launched. */
template <>
class Logger<true> {
    std::ofstream stream;
    size_t _indent_level;
    std::string _language;

public:

    Logger() : _indent_level(0), _language("c++     ") {
        using clock = std::chrono::system_clock;
        auto now = clock::to_time_t(clock::now());
        auto format = *std::localtime(&now);

        std::ostringstream name;
        name << "debug_" << std::put_time(&format, "%Y-%m-%d_%H-%M-%S");
        name << ".log";

        stream.open(name.str(), std::ios::app);
        if (!stream.is_open()) {
            std::cerr << "failed to open log file: " << name.str() << std::endl;
        }
    }

    template <typename... Args>
    inline void operator()(Args&&... messages) {
        if constexpr (sizeof...(messages) > 0) {
            if (stream.is_open()) {
                stream << _language;
                for (size_t i = 0; i < _indent_level; ++i) {
                    stream << "    ";
                }
                (stream << ... << std::forward<Args>(messages));
                stream << std::endl;
            }
        }
    }

    inline void indent() {
        ++_indent_level;
    }

    inline void unindent() {
        if (_indent_level > 0) {
            --_indent_level;
        }
    }

    inline void reset_indent() {
        _indent_level = 0;
    }

    inline size_t indent_level() const {
        return _indent_level;
    }

    inline void language(const std::string& lang) {
        if (lang.size() > 8) {
            std::ostringstream msg;
            msg << "language name must be 8 characters or less: " << lang;
            throw std::runtime_error(msg.str());
        }
        _language = lang;
    }

};


/* Global logging object. */
Logger<DEBUG> LOG;


/* An RAII guard to control nested indentation in the log file.  In the case where
DEBUG=false, this is a no-op and will be optimized away by the compiler. */
struct LogGuard {
    
    inline LogGuard() {
        if constexpr (DEBUG) {
            LOG.indent();
        }
    }

    inline ~LogGuard() {
        if constexpr (DEBUG) {
            LOG.unindent();
        }
    }

};


/* macros to write simple statements to the log file.  These avoids the need to place
`if constexpr (DEBUG)` guards around every logging statement, as long as no other logic
is needed within the constexpr branch itself. */
#define WRITE_LOG(...) if constexpr (DEBUG) { LOG(__VA_ARGS__); }
#define INDENT_LOG(...) \
    if constexpr (DEBUG) { \
        LOG(__VA_ARGS__); \
    } \
    LogGuard _log_guard_##__LINE__;
#define PYINDENT_LOG(...) \
    if constexpr (DEBUG) { \
        LOG.language("py      "); \
        LOG(__VA_ARGS__); \
        LOG.language("c++     "); \
    } \
    LogGuard _log_guard_##__LINE__;


}  // namespace bertrand


#endif // BERTRAND_STRUCTS_UTIL_BASE_H
