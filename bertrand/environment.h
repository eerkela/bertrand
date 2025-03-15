#ifndef BERTRAND_ENVIRONMENT_H
#define BERTRAND_ENVIRONMENT_H

#include "bertrand/common.h"
#include "bertrand/static_str.h"


namespace bertrand {


/// TODO: full virtual environment interactions at some point in the future.


namespace impl {
    struct virtualenv;
    static virtualenv get_virtual_environment() noexcept;

    struct virtualenv {
    private:
        friend virtualenv get_virtual_environment() noexcept;

        virtualenv() = default;

    public:
        std::filesystem::path path = [] {
            if (const char* path = std::getenv("BERTRAND_HOME")) {
                return std::filesystem::path(path);
            }
            return std::filesystem::path();
        }();
        std::filesystem::path bin = *this ? path / "bin" : std::filesystem::path();
        std::filesystem::path lib = *this ? path / "lib" : std::filesystem::path();
        std::filesystem::path include = *this ? path / "include" : std::filesystem::path(); 
        std::filesystem::path modules = *this ? path / "modules" : std::filesystem::path();

        virtualenv(const virtualenv&) = delete;
        virtualenv(virtualenv&&) = delete;
        virtualenv& operator=(const virtualenv&) = delete;
        virtualenv& operator=(virtualenv&&) = delete;

        explicit operator bool() const noexcept {
            return !path.empty();
        }
    };

    static virtualenv get_virtual_environment() noexcept {
        return virtualenv();
    }

    inline bool is_debugger_present() noexcept {
        #if defined(_WIN32)
            return IsDebuggerPresent();

        #elif defined(__APPLE__)
            int mib[4];
            struct kinfo_proc info;
            size_t size = sizeof(info);
            info.kp_proc.p_flag = 0;
            mib[0] = CTL_KERN;
            mib[1] = KERN_PROC;
            mib[2] = KERN_PROC_PID;
            mib[3] = getpid();
            if (sysctl(mib, 4, &info, &size, nullptr, 0) != 0) {
                return false;
            }
            return (info.kp_proc.p_flag & P_TRACED) != 0;
    
        #elif defined(__unix__)
            FILE* status = fopen("/proc/self/status", "r");
            if (!status) {
                return false;
            }
            char buffer[256];
            bool debugged = false;
            while (fgets(buffer, sizeof(buffer), status)) {
                if (strncmp(buffer, "TracerPid:", 10) == 0) {
                    int pid = atoi(buffer + 10);
                    if (pid != 0) {
                        debugged = true;
                    }
                    break;
                }
            }
            fclose(status);
            return debugged;

        #else
            return false;
        #endif
    }

}


/* A simple struct holding paths to the bertrand environment's directories, if such an
environment is currently active. */
inline const impl::virtualenv VIRTUAL_ENV = impl::get_virtual_environment();


/* Place a breakpoint at the current line, causing the program to drop into an
interactive debug session at that location.  This can only be used when
`DEBUG == true`, and fails to compile otherwise. */
template <typename Dummy = void> requires (DEBUG)
[[gnu::always_inline]] inline void breakpoint() noexcept {
    if (impl::is_debugger_present()) {
        #ifdef _MSC_VER
            __debugbreak();  // MSVC
        #elif defined(__clang__) || defined(__GNUC__)
            __builtin_debugtrap();  // clang and GCC
        #else
            raise(SIGTRAP);  // POSIX fallback
        #endif
    }
}


/* A python-style `assert` statement in C++, which is optimized away if built with
`bertrand::DEBUG == false` (release mode).  This differs from the built-in C++
`assert()` macro in that this is implemented as a normal inline function that accepts
a format string and arguments (which are automatically passed through `repr()`), and
results in a `bertrand::AssertionError` with a coherent traceback, which can be
seamlessly passed up to Python.  It is thus possible to implement pytest-style unit
tests using this function just as in native Python. */
template <typename... Args>
[[gnu::always_inline]] void assert_(
    bool cnd,
    impl::format_repr<Args...> msg = "",
    Args&&... args
) noexcept(!DEBUG) {
    if constexpr (DEBUG) {
        if (!cnd) {
            throw AssertionError(std::format(
                msg,
                impl::to_format_repr(std::forward<Args>(args))...
            ));
        }
    }
}


/* A python-style `assert` statement in C++, which is optimized away if built with
`bertrand::DEBUG == false` (release mode).  This differs from the built-in C++
`assert()` macro in that this is implemented as a normal inline function that accepts
an arbitrary value (which is automatically passed through `repr()`), and results in a
`bertrand::AssertionError` with a coherent traceback, which can be seamlessly passed up
to Python.  It is thus possible to implement pytest-style unit tests using this
function just as in native Python. */
template <typename T>
[[gnu::always_inline]] void assert_(bool cnd, T&& obj) noexcept(!DEBUG) {
    if constexpr (DEBUG) {
        if (!cnd) {
            throw AssertionError(repr(std::forward<T>(obj)));
        }
    }
}


}


#endif
