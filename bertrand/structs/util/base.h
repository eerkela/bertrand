#ifndef BERTRAND_STRUCTS_UTIL_BASE_H
#define BERTRAND_STRUCTS_UTIL_BASE_H

#include <cstddef>  // size_t
#include <chrono>  // std::chrono::system_clock
#include <fstream>  // std::ofstream
#include <iomanip>  // std::put_time()
#include <ios>  // std::ios::app
#include <iostream>  // std::cerr
#include <sstream>
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


// TODO: use index operator to select a logging tag?

// LOG["info"](...)
// LOG["err"](...)



/* Enum struct that lists the tags available for logging purposes.  One of these must
be specified as the first argument to a logging macro, and will be inserted as a
bracketed prefix to the beginning of the log message. */
struct LogTag {
    static constexpr std::string_view info {"[info]  "};
    static constexpr std::string_view err  {"[err]   "};
    static constexpr std::string_view ref  {"[ref]   "};
    static constexpr std::string_view mem  {"[mem]   "};
    static constexpr std::string_view init {"[init]  "};
};


/* Specialization for when logging is disabled.  This raises a compile-time error if
the logger is used without being guarded by an `if constexpr (DEBUG)` branch. */
template <bool Enable>
struct Logger {
    std::string _dummy;

    inline const std::string& language() const {
        static_assert(Enable, "logging is not enabled.");
        return _dummy;
    }

    inline void language(const std::string& lang) {
        static_assert(Enable, "logging is not enabled.");
    }

    inline const std::string& address() const {
        static_assert(Enable, "logging is not enabled.");
        return _dummy;
    }

    inline void address(const void* addr) {
        static_assert(Enable, "logging is not enabled.");
    }

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
    std::string _address;
    std::string _prev_address;

public:

    Logger() : _indent_level(0), _language("c++ ") {
        using Clock = std::chrono::system_clock;
        address(nullptr);

        std::ostringstream name;
        auto now = Clock::to_time_t(Clock::now());
        auto format = *std::localtime(&now);
        name << "debug_" << std::put_time(&format, "%Y-%m-%d_%H-%M-%S");
        name << ".log";

        stream.open(name.str(), std::ios::app);
        if (!stream.is_open()) {
            std::cerr << "failed to open log file: " << name.str() << std::endl;
        }
    }

    inline const std::string& language() const {
        return _language;
    }

    void language(const std::string& lang) {
        if (lang.size() > 3) {
            std::ostringstream msg;
            msg << "language name must be 3 characters or less: " << lang;
            throw std::runtime_error(msg.str());
        }
        _language = lang + std::string(4 - lang.size(), ' ');
    }

    inline const std::string& address() const {
        return _address;
    }

    void address(const void* ptr) {
        std::ostringstream addr;
        if (ptr == nullptr) {
            addr << this;
            if (_address.empty()) {
                _address = std::string(addr.str().size() + 4, ' ');
                _prev_address = _address;
            } else {
                _prev_address = _address;
                _address = std::string(addr.str().size() + 4, ' ');
            }
        } else {
            addr << ptr;
            std::string addr_str = addr.str();
            _prev_address = _address;
            _address = addr_str + std::string(
                _address.size() - addr_str.size(), ' '
            );
        }
    }

    template <typename... Args>
    inline void operator()(Args&&... messages) {
        if constexpr (sizeof...(messages) > 0) {
            if (stream.is_open()) {
                stream << _language << _address;
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
        _address = _prev_address;
        if (_indent_level > 0) {
            --_indent_level;
        }
    }

};


/* Global logging object. */
Logger<DEBUG> LOGGER;


/* An RAII guard to control nested indentation in the log file.  In the case where
DEBUG=false, this is a no-op and will be optimized away by the compiler. */
template <bool Enable>
struct LogGuard {

    inline LogGuard() {
        LOGGER.indent();
    }

    inline ~LogGuard() {
        LOGGER.unindent();
    }

};


// LOG(info, "hello world");
// LOG_CONTEXT(info, this, "hello world");


/* Macros to write simple statements to the log file.  These avoid the need to place
`if constexpr (DEBUG)` guards around every logging statement, as long as no other logic
is needed within the constexpr branch itself.

The _CONTEXT variants are used to indent a block of logging statements and associate
them with a particular memory address.  If the address is set to nullptr, it will
omit the address from the log statement, which may be useful for static methods or
other functions that aren't attached to a particular object.  These macros then produce
an RAII-style guard in the calling context that automatically unindents the log and
restores the previous memory address when the guard goes out of scope.
*/
#ifdef BERTRAND_DEBUG
    #define LOG(...) \
        LOGGER(__VA_ARGS__);

    #define PYLOG(...) \
        LOGGER.language("py"); \
        LOGGER(__VA_ARGS__); \
        LOGGER.language("c++");

    #define LOG_CONTEXT(addr, ...) \
        LOGGER.address(addr); \
        LOGGER(__VA_ARGS__); \
        LogGuard<DEBUG> _log_guard_##__LINE__;

    #define PYLOG_CONTEXT(addr, ...) \
        LOGGER.language("py"); \
        LOGGER.address(addr); \
        LOGGER(__VA_ARGS__); \
        LOGGER.language("c++"); \
        LogGuard<DEBUG> _log_guard_##__LINE__;

#else
    #define LOG(...)
    #define PYLOG(...)
    #define LOG_CONTEXT(addr, ...)
    #define PYLOG_CONTEXT(addr, ...)

#endif


/* Logging throws an incorrect warning about uninitialized variables when logging
constructors, so we have to insert these pragmas around the constructors to suppress
warnings on major compilers.
*/
#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#elif defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wuninitialized"
#elif defined(_MSC_VER)
    #pragma warning(push)
    #pragma warning(disable: 26494)  // VAR_USE_BEFORE_INIT
    #pragma warning(disable: 26495)  // MEMBER_UNINIT
#endif

#if defined(__GNUC__) && !defined(__clang__)
    #pragma GCC diagnostic pop
#elif defined(__clang__)
    // #pragma clang diagnostic pop
#elif defined(_MSC_VER)
    #pragma warning(pop)
#endif


}  // namespace bertrand


#endif // BERTRAND_STRUCTS_UTIL_BASE_H
