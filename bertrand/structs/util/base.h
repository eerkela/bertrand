#ifndef BERTRAND_STRUCTS_UTIL_BASE_H
#define BERTRAND_STRUCTS_UTIL_BASE_H

#include <cstddef>  // size_t
#include <chrono>  // std::chrono::system_clock
#include <fstream>  // std::ofstream
#include <iomanip>  // std::put_time()
#include <ios>  // std::ios::app
#include <iostream>  // std::cerr
#include <sstream>  // std::ostringstream
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
be dumped to a .log file in the current working directory.
*/
#ifdef BERTRAND_DEBUG
    inline constexpr bool DEBUG = true;
#else
    inline constexpr bool DEBUG = false;
#endif


/* Enum struct that lists the language levels available for logging purposes.  One of
these is always inserted as a prefix to the beginning of the log message, and encoding
them as a struct guarantees that they remain consistent across the codebase. */
struct LogLang {
    static constexpr std::string_view py  {"py  "};
    static constexpr std::string_view cpp {"c++ "};
};


/* Enum struct that lists the tags available for logging purposes.  One of these must
be specified as the first argument to a logging macro, and will be inserted as a
bracketed prefix to the beginning of the log message. */
struct LogTag {
    static constexpr std::string_view info {"[info]  "};
    static constexpr std::string_view err  {"[err]   "};
    static constexpr std::string_view ref  {"[ref]   "};
    static constexpr std::string_view mem  {"[mem]   "};
    static constexpr std::string_view init {"[init]  "};
    static constexpr std::string_view call {"[call]  "};
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

    inline const std::string& tag() const {
        static_assert(Enable, "logging is not enabled.");
        return _dummy;
    }

    inline const std::string& address() const {
        static_assert(Enable, "logging is not enabled.");
        return _dummy;
    }

    inline void language(const std::string_view& lang) {
        static_assert(Enable, "logging is not enabled.");
    }

    inline void tag(const std::string_view& tag) {
        static_assert(Enable, "logging is not enabled.");
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

    struct Guard {

        inline Guard(Logger& logger) {
            static_assert(Enable, "logging is not enabled.");
        }

    };

    inline Guard indent_guard() {
        static_assert(Enable, "logging is not enabled.");
        return Guard(*this);
    }

};


/* Specialization for when logging is enabled.  This dumps log statements to a .log
file in the current working directory.  If the data structures are used from Python,
then this will be the working directory of the Python interpreter itself, which is
usually the location from which the interpreter was launched. */
template <>
class Logger<true> {
    using Clock = std::chrono::system_clock;

    Clock::time_point _start_time;
    std::ofstream stream;
    size_t _indent_level;
    std::string _language;
    std::string _tag;
    std::string _address;
    std::string _prev_address;

public:

    Logger() : _start_time(Clock::now()), _indent_level(0), _language("c++ ") {
        address(nullptr);

        std::ostringstream filename;
        auto now = Clock::to_time_t(_start_time);
        auto format = std::put_time(std::localtime(&now), "%Y-%m-%d_%H-%M-%S");

        filename << "debug_" << format << ".log";
        stream.open(filename.str(), std::ios::app);
        if (!stream.is_open()) {
            std::cerr << "failed to open log file: " << filename.str() << std::endl;
        }
    }

    inline const std::string& language() const {
        return _language;
    }

    inline const std::string& tag() const {
        return _tag;
    }

    inline const std::string& address() const {
        return _address;
    }

    void language(const std::string_view& lang) {
        _language = lang;
    }

    void tag(const std::string_view& tag) {
        _tag = tag;
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
        if (stream.is_open()) {
            // calculate relative timestamp
            auto elapsed = Clock::now() - _start_time;
            double seconds = std::chrono::duration<double>(elapsed).count();

            // format timestamp to fixed width
            std::ostringstream timestamp_stream;
            timestamp_stream << std::fixed << std::setprecision(5) << seconds;
            std::string timestamp = timestamp_stream.str();
            if (timestamp.size() < 7) {
                timestamp = timestamp + std::string(7 - timestamp.size(), ' ');
            } else if (timestamp.size() > 7) {
                timestamp = timestamp.substr(0, 7);
            }

            // write log entry
            stream << timestamp << " " << _language << _tag << _address;
            for (size_t i = 0; i < _indent_level; ++i) {
                stream << "    ";
            }
            (stream << ... << std::forward<Args>(messages));
            stream << std::endl;
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

    struct Guard {
        Logger& logger;

        inline Guard(Logger& logger) : logger(logger) {
            logger.indent();
        }

        inline ~Guard() {
            logger.unindent();
        }
    };

    inline Guard indent_guard() {
        return Guard(*this);
    }

};


/* Global logging object. */
Logger<DEBUG> LOGGER;


/* Macros to write simple statements to the log file.  These avoid the need to place
`if constexpr (DEBUG)` guards around every logging statement, as long as no other logic
is needed within the constexpr branch itself.

The _CONTEXT variants are used to indent a block of logging statements and associate
them with a particular memory address.  If the address is set to nullptr, it will
omit the address from the log statement, which may be useful for static methods or
other functions that aren't attached to any particular object.  These macros then
produce an RAII-style guard in the calling context that automatically unindents the log
and restores the previous memory address when the guard goes out of scope.
*/
#ifdef BERTRAND_DEBUG
    #define LOG(TAG, ...) \
        LOGGER.tag(LogTag::TAG); \
        LOGGER(__VA_ARGS__);

    #define PYLOG(TAG, ...) \
        LOGGER.language(LogLang::py); \
        LOGGER.tag(LogTag::TAG); \
        LOGGER(__VA_ARGS__); \
        LOGGER.language(LogLang::cpp);

    #define LOG_CONTEXT(TAG, ADDR, ...) \
        LOGGER.tag(LogTag::TAG); \
        LOGGER.address(ADDR); \
        LOGGER(__VA_ARGS__); \
        Logger<DEBUG>::Guard _log_guard_##__LINE__ = LOGGER.indent_guard();

    #define PYLOG_CONTEXT(TAG, ADDR, ...) \
        LOGGER.language(LogLang::py); \
        LOGGER.tag(LogTag::TAG); \
        LOGGER.address(ADDR); \
        LOGGER(__VA_ARGS__); \
        LOGGER.language(LogLang::cpp); \
        Logger<DEBUG>::Guard _log_guard_##__LINE__ = LOGGER.indent_guard();

#else
    #define LOG(TAG, ...)
    #define PYLOG(TAG, ...)
    #define LOG_CONTEXT(TAG, ADDR, ...)
    #define PYLOG_CONTEXT(TAG, ADDR, ...)

#endif


/* Logging throws an incorrect warning due to uninitialized `this` pointer when logging
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
    #pragma clang diagnostic pop
#elif defined(_MSC_VER)
    #pragma warning(pop)
#endif


}  // namespace bertrand


#endif // BERTRAND_STRUCTS_UTIL_BASE_H
