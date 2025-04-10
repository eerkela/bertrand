#ifndef BERTRAND_EXCEPT_H
#define BERTRAND_EXCEPT_H

#include "bertrand/common.h"

#include <cpptrace/cpptrace.hpp>


namespace bertrand {


namespace impl {

    /* In the event of a syscall error, get an explanatory error message in a
    thread-safe way. */
    [[nodiscard]] inline std::string system_err_msg() {
        #ifdef _WIN32
            return
                std::string("windows: ") +
                std::system_category().message(GetLastError());  // thread-safe
        #elifdef __unix__
            char buffer[1024];
            return
                std::string("unix: ") +
                strerror_r(errno, buffer, 1024);  // thread-safe
        #else
            return "Unknown OS error";
        #endif
    }

}


/* CPython exception types:
*      https://docs.python.org/3/c-api/exceptions.html#standard-exceptions
*
* Inheritance hierarchy:
*      https://docs.python.org/3/library/exceptions.html#exception-hierarchy
*/


/// TODO: it might be possible to extract the backing union from the `Exception` class
/// and use it within `Expected<>` to densely pack all of the discriminators.  That
/// would reduce the overhead of expecteds down to 24-32 bytes, depending on whether
/// there are multiple error states, which is amazingly efficient, and perfectly
/// matches `std::expected`.

/// TODO: the only trick here is that I need to swap the internals over to using
/// custom reference counting rather than a unique pointer, since otherwise copy
/// semantics would get unnecessarily expensive.  This is probably a good idea anyways,
/// but most importantly, it means that accessing `expected.error()` will always
/// return a new error object, which might be a trivial copy.





/// TODO: in C++26 with constexpr exceptions, `Exception` can inherit from
/// std::exception, but not before, since its constructors are not marked as constexpr


/* The root of the bertrand exception hierarchy.  This and all its subclasses are
usable just like their built-in Python equivalents, and maintain coherent stack traces
across both languages.  If an exception is constructed at compile time, then the
C++ stack trace will be omitted.  If Python is not loaded, then the same exception
types can still be used in a pure C++ context, but the `from_python()` and
`to_python()` methods will be disabled. */
struct Exception {
private:

    /* Exception internals are stored in a tagged union to differentiate between
    compile time and runtime exceptions. */
    union storage {
        /* At compile time, all we store is the message, without any traceback info.
        This is held as a pointer to a raw character buffer with static storage
        duration in order to equalize the size of the compile-time and runtime
        cases. */
        const char* compile_time;

        /* At runtime, we also store a full traceback to the error location, as well
        as a mutable cache for the `what()` message.  This information is stored in a
        unique pointer so as not to contribute to the `Exception`'s overall size. */
        struct type {
            /// TODO: figure out skips to hide exception internals
            mutable size_t skip;
            mutable bool resolved;
            union Trace {
                mutable cpptrace::raw_trace raw;
                mutable cpptrace::stacktrace full;
                constexpr ~Trace() noexcept {}
            } trace;
            mutable std::string what;
            size_t length;

            /* The exception message is inlined directly after this header struct. */
            char* data() noexcept {
                return reinterpret_cast<char*>(this + 1);
            }

            /* The exception message is inlined directly after this header struct. */
            const char* data() const noexcept {
                return reinterpret_cast<const char*>(this + 1);
            }

            type(std::string_view msg) noexcept :
                skip(0),
                resolved(false),
                trace{.raw = cpptrace::generate_raw_trace(1)}, // skip this constructor
                length(msg.size())
            {
                std::copy_n(msg.data(), msg.size(), data());
            }

            type(
                cpptrace::raw_trace&& trace,
                size_t skip,
                std::string_view msg
            ) noexcept :
                skip(skip),
                resolved(false),
                trace{.raw = std::move(trace)},
                length(msg.size())
            {
                std::copy_n(msg.data(), msg.size(), data());
            }

            type(
                cpptrace::stacktrace&& trace,
                size_t skip,
                std::string_view msg
            ) noexcept :
                skip(skip),
                resolved(true),
                trace{.full = std::move(trace)},
                length(msg.size())
            {
                std::copy_n(msg.data(), msg.size(), data());
            }

            type(const type& other) noexcept :
                skip(other.skip),
                resolved(other.resolved),
                trace(resolved ?
                    Trace{.raw = other.trace.raw} :
                    Trace{.full = other.trace.full}
                ),
                what(other.what),
                length(other.length)
            {
                std::copy_n(other.data(), other.length, data());
            }

            type& operator=(const type& other) noexcept {
                skip = other.skip;
                if (resolved) {
                    if (other.resolved) {
                        trace.full = other.trace.full;
                    } else {
                        std::destroy_at(&trace.full);
                        std::construct_at(&trace.raw, other.trace.raw);
                    }
                } else {
                    if (other.resolved) {
                        std::destroy_at(&trace.raw);
                        std::construct_at(&trace.full, other.trace.full);
                    } else {
                        trace.raw = other.trace.raw;
                    }
                }
                resolved = other.resolved;
                what = other.what;
                length = other.length;
                std::copy_n(other.data(), other.length, data());
                return *this;
            }

            constexpr ~type() noexcept {
                if (resolved) {
                    std::destroy_at(&trace.full);
                } else {
                    std::destroy_at(&trace.raw);
                }
            }
        };

        /* The unique pointer also includes enough buffer space to store the exception
        message inline, in order to limit heap allocations.  The `type` header will
        occupy the first `sizeof(type)` bytes of the buffer, followed by the message,
        whose length is included in the header. */
        std::unique_ptr<char[]> run_time;

        /* Access the header of the run_time buffer. */
        type* get() noexcept {
            return reinterpret_cast<type*>(run_time.get());
        }

        /* Access the header of the run_time buffer. */
        const type* get() const noexcept {
            return reinterpret_cast<const type*>(run_time.get());
        }

        constexpr ~storage() noexcept {}
    } m_storage;
    bool m_compiled;

    static std::string format_frame(const cpptrace::stacktrace_frame& frame) {
        std::string result = "    File \"" + frame.filename + "\", line ";
        if (frame.line.has_value()) {
            result += std::to_string(frame.line.value()) + ", in ";
        } else {
            result += "<unknown>, in ";
        }
        if (frame.is_inline) {
            result += "[inline] ";
        }
        result += frame.symbol + "\n";
        return result;
    }

protected:
    struct get_trace {
        size_t skip = 1;
        constexpr get_trace operator++(int) const noexcept { return {skip + 1}; }
    };

    constexpr explicit Exception(get_trace trace, std::string_view msg) noexcept :
        m_storage([](get_trace& trace, std::string_view msg) noexcept -> storage {
            if consteval {
                return {.compile_time = msg.data()};
            } else {
                std::unique_ptr p = std::make_unique<char[]>(
                    sizeof(typename storage::type) + msg.size()
                );
                std::construct_at(
                    reinterpret_cast<typename storage::type*>(
                        p.get()
                    ),
                    cpptrace::generate_raw_trace(1),
                    trace.skip,
                    msg
                );
                return {.run_time = std::move(p)};
            }
        }(trace, msg)),
        m_compiled(std::is_constant_evaluated())
    {}

public:
    [[nodiscard]] constexpr explicit Exception(
        std::string_view msg = {}
    ) noexcept :
        m_storage([](auto&& msg) noexcept -> storage {
            if consteval {
                return {.compile_time = msg.data()};
            } else {
                std::unique_ptr p = std::make_unique<char[]>(
                    sizeof(typename storage::type) + msg.size()
                );
                std::construct_at(
                    reinterpret_cast<typename storage::type*>(
                        p.get()
                    ),
                    msg
                );
                return {.run_time = std::move(p)};
            }
        }(msg)),
        m_compiled(std::is_constant_evaluated())
    {}

    [[nodiscard]] explicit Exception(
        cpptrace::raw_trace&& trace,
        std::string_view msg = {}
    ) noexcept :
        m_storage{.run_time = std::make_unique<char[]>(
            sizeof(typename storage::type) + msg.size()
        )},
        m_compiled(false)
    {
        std::construct_at(
            m_storage.get(),
            std::move(trace),
            0,
            msg
        );
    }

    [[nodiscard]] explicit Exception(
        cpptrace::stacktrace&& trace,
        std::string_view msg = {}
    ) noexcept :
        m_storage{.run_time = std::make_unique<char[]>(
            sizeof(typename storage::type) + msg.size()
        )},
        m_compiled(false)
    {
        std::construct_at(
            m_storage.get(),
            std::move(trace),
            0,
            msg
        );
    }

    [[nodiscard]] constexpr Exception(const Exception& other) noexcept :
        m_storage([](const Exception& other) noexcept -> storage {
            if consteval {
                return {.compile_time = other.m_storage.compile_time};
            } else {
                if (other.compiled()) {
                    return {.compile_time = other.m_storage.compile_time};
                }
                auto* o = other.m_storage.get();
                std::unique_ptr p = std::make_unique<char[]>(
                    sizeof(typename storage::type) + o->length
                );
                std::construct_at(
                    reinterpret_cast<typename storage::type*>(
                        p.get()
                    ),
                    *other.m_storage.get()
                );
                return {.run_time = std::move(p)};
            }
        }(other)),
        m_compiled(other.compiled())
    {}

    [[nodiscard]] constexpr Exception(Exception&& other) noexcept :
        m_storage([](Exception&& other) noexcept -> storage {
            if consteval {
                return {.compile_time = other.m_storage.compile_time};
            } else {
                if (other.compiled()) {
                    return {.compile_time = other.m_storage.compile_time};
                }
                return {.run_time = std::move(other.m_storage.run_time)};
            }
        }(std::move(other))),
        m_compiled(other.compiled())
    {}

    constexpr Exception& operator=(const Exception& other) noexcept {
        if (this != &other) {
            if consteval {
                m_storage.compile_time = other.m_storage.compile_time;
            } else {
                if (compiled()) {
                    if (other.compiled()) {
                        m_storage.compile_time = other.m_storage.compile_time;
                    } else {
                        std::destroy_at(&m_storage.compile_time);
                        std::construct_at(
                            &m_storage.run_time,
                            std::make_unique<char[]>(
                                sizeof(typename storage::type) +
                                other.m_storage.get()->length
                            )
                        );
                        std::construct_at(
                            m_storage.get(),
                            *other.m_storage.get()
                        );
                    }
                } else {
                    if (other.compiled()) {
                        std::destroy_at(&m_storage.run_time);
                        std::construct_at(
                            &m_storage.compile_time,
                            other.m_storage.compile_time
                        );
                    } else {
                        auto* s = m_storage.get();
                        auto* o = other.m_storage.get();
                        if (o->length <= s->length) {  // reuse the same buffer
                            *s = *o;
                        } else {  // allocate a new buffer
                            m_storage.run_time = std::make_unique<char[]>(
                                sizeof(typename storage::type) + o->length
                            );
                            std::construct_at(m_storage.get(), *o);
                        }
                    }
                }
            }
            m_compiled = other.compiled();
        }
        return *this;
    }

    constexpr Exception& operator=(Exception&& other) noexcept {
        if (this != &other) {
            if consteval {
                m_storage.compile_time = other.m_storage.compile_time;
            } else {
                if (compiled()) {
                    if (other.compiled()) {
                        m_storage.compile_time = other.m_storage.compile_time;
                    } else {
                        std::destroy_at(&m_storage.compile_time);
                        std::construct_at(
                            &m_storage.run_time,
                            std::move(other.m_storage.run_time)
                        );
                    }
                } else {
                    if (other.compiled()) {
                        std::destroy_at(&m_storage.run_time);
                        std::construct_at(
                            &m_storage.compile_time,
                            std::move(other.m_storage.compile_time)
                        );
                    } else {
                        m_storage.run_time = std::move(other.m_storage.run_time);
                    }
                }
            }
        }
        return *this;
    }

    constexpr virtual ~Exception() noexcept {
        if consteval {
            std::destroy_at(&m_storage.compile_time);
        } else {
            if (compiled()) {
                std::destroy_at(&m_storage.compile_time);
            } else {
                std::destroy_at(&m_storage.run_time);
            }
        }
    }

    /* `True` if the Exception was created at compile time.  `False` if it was created
    at runtime.  Compile-time exceptions will not store a stack trace, and will store
    the string as a `string_view`. */
    [[nodiscard]] constexpr bool compiled() const noexcept { return m_compiled; }

    /* Skip the `n` most recent frames in the stack trace.  Note that this works by
    incrementing an internal counter, so no extra traces are resolved at runtime, and
    it is not guaranteed that the first skipped frame is the current one, unless all
    earlier frames have been already been skipped in a similar fashion.  Forwards the
    exception itself for simplified chaining (e.g. `throw exc.skip(2)`). */
    template <typename Self>
    constexpr decltype(auto) skip(this Self&& self, size_t n = 0) noexcept {
        if !consteval {
            if (self.compiled()) {
                return (std::forward<Self>(self));
            }
            ++n;  // always skip this method
            typename storage::type& s = *self.m_storage.run_time;
            if (s.resolved) {
                if (n >= s.trace.full.frames.size()) {
                    s.trace.full.frames.clear();
                } else {
                    s.trace.full.frames.erase(
                        s.trace.full.frames.begin(),
                        s.trace.full.frames.begin() + n
                    );
                }
            }
            s.skip += n;
        }
        return (std::forward<Self>(self));
    }

    /* Discard any frames that are more recent than the frame in which this method was
    invoked, or an earlier frame if an offset is supplied.  Forwards the exception
    itself for simplified chaining (e.g. `throw exc.trim_before()`), and also resets
    the `skip()` counter to start counting from the current frame. */
    template <typename Self>
    constexpr decltype(auto) trim_before(this Self&& self, size_t offset = 0) noexcept {
        if !consteval {
            if (self.compiled()) {
                return (std::forward<Self>(self));
            }
            ++offset;  // always skip this method
            cpptrace::raw_trace curr = cpptrace::generate_raw_trace();
            if (offset > curr.frames.size()) {
                return std::forward<Self>(self);  // no frames to cut
            }
            cpptrace::frame_ptr pivot = curr.frames[curr.frames.size() - offset];
            typename storage::type& s = *self.m_storage.run_time;
            if (s.resolved) {
                for (size_t i = s.trace.full.frames.size(); i-- > s.skip;) {
                    if (s.trace.full.frames[i].raw_address == pivot) {
                        s.trace.full.frames.erase(
                            s.trace.full.frames.begin(),
                            s.trace.full.frames.begin() + i
                        );
                        s.skip = 0;
                        break;
                    }
                }
            } else {
                for (size_t i = s.trace.raw.frames.size(); i-- > s.skip;) {
                    if (s.trace.raw.frames[i] == pivot) {
                        s.trace.raw.frames.erase(
                            s.trace.raw.frames.begin(),
                            s.trace.raw.frames.begin() + i
                        );
                        s.skip = 0;
                        break;
                    }
                }
            }
        }
        return (std::forward<Self>(self));
    }

    /* Discard any frames that are less recent than the frame in which this method was
    invoked, or a later frame if an offset is supplied.  Forwards the exception
    itself for simplified chaining (e.g. `throw exc.trim_after()`) */
    template <typename Self>
    constexpr decltype(auto) trim_after(this Self&& self, size_t offset = 0) noexcept {
        if !consteval {
            if (self.compiled()) {
                return (std::forward<Self>(self));
            }
            ++offset;  // always skip this method
            cpptrace::raw_trace curr = cpptrace::generate_raw_trace();
            if (offset > curr.frames.size()) {
                return std::forward<Self>(self);  // no frames to cut
            }
            cpptrace::frame_ptr pivot = curr.frames[offset];
            typename storage::type& s = *self.m_storage.run_time;
            if (s.resolved) {
                for (size_t i = s.skip; i < s.trace.full.frames.size(); ++i) {
                    if (s.trace.full.frames[i].raw_address == pivot) {
                        s.trace.full.frames.resize(i + 1);
                        break;
                    }
                }
            } else {
                for (size_t i = s.skip; i < s.trace.raw.frames.size(); ++i) {
                    if (s.trace.raw.frames[i] == pivot) {
                        s.trace.raw.frames.resize(i + 1);
                        break;
                    }
                }
            }
        }
        return (std::forward<Self>(self));
    }

    /* A resolved trace to the source location where the error occurred, with internal
    C++/Python frames removed.  The trace is lazily loaded directly from the program
    counter when first accessed (typically only when an unhandled exception is
    displayed via the `what()` method).  This may return a null pointer if the
    exception has no traceback to report, which only occurs when an exception is thrown
    in a constexpr context (C++26 and later). */
    [[nodiscard]] const cpptrace::stacktrace* trace() const noexcept {
        if consteval {
            return nullptr;
        } else {
            if (m_compiled) {
                return nullptr;
            }
            const typename storage::type& r = *m_storage.get();
            if (r.resolved) {
                return &r.trace.full;
            }

            cpptrace::stacktrace trace = r.trace.raw.resolve();
            cpptrace::stacktrace filtered;
            if (r.skip < trace.frames.size()) {
                filtered.frames.reserve(trace.frames.size() - r.skip);
            }
            for (size_t i = r.skip; i < trace.frames.size(); ++i) {
                cpptrace::stacktrace_frame& frame = trace.frames[i];
                if constexpr (!DEBUG) {
                    if (frame.symbol.starts_with("__")) {
                        continue;  // filter out C++ internals in release mode
                    }
                }
                filtered.frames.emplace_back(std::move(frame));
            }
            r.trace.raw.~raw_trace();
            std::construct_at(&r.trace.full, std::move(filtered));
            r.resolved = true;
            return &r.trace.full;
        }
    }

    /* The raw text of the exception message, sans traceback. */
    [[nodiscard]] constexpr std::string_view message() const noexcept {
        if consteval {
            return m_storage.compile_time;
        } else {
            if (m_compiled) {
                return m_storage.compile_time;
            }
            return {m_storage.get()->data(), m_storage.get()->length};
        }
    }

    /* The full exception diagnostic, including a coherent, Python-style traceback and
    error text.  If the exception was constructed at compile time, then the traceback
    will be omitted. */
    [[nodiscard]] constexpr const char* what() const noexcept {
        if consteval {
            return m_storage.compile_time;
        } else {
            if (m_compiled) {
                return m_storage.compile_time;
            }
            const typename storage::type& r = *m_storage.get();
            if (r.what.empty()) {
                r.what = "Traceback (most recent call last):\n";
                if (const cpptrace::stacktrace* trace = this->trace()) {
                    for (size_t i = trace->frames.size(); i-- > 0;) {
                        r.what += format_frame(trace->frames[i]);
                    }
                }
                r.what += std::string_view{r.data(), r.length};
            }
            return r.what.data();
        }
    }

    /* Clear the exception's what() cache, forcing it to be recomputed the next time
    it is requested. */
    void flush() noexcept {
        m_storage.get()->what.clear();
    }

    /* A type index for this exception, which can be searched in the global
    `to_python()` map to find a corresponding callback. */
    [[nodiscard]] virtual std::type_index type() const noexcept {
        return typeid(Exception);
    }

    /* Re-raise this error as the proper type if it has been caught polymorphically. */
    [[noreturn]] virtual void raise() const {
        throw *this;
    }

    /* Re-raise this error as the proper type if it has been caught polymorphically.
    The error will be moved into C++'s internal  */
    [[noreturn]] virtual void raise() && {
        throw std::move(*this);
    }

    /* Throw the most recent C++ exception as a corresponding Python error, pushing it
    onto the active interpreter.  If there is no unhandled exception for this thread or
    no callback could be found (for instance if Python isn't loaded), then this will
    terminate the program instead. */
    static void to_python() noexcept;

    /* Catch an exception from Python, re-throwing it as an equivalent C++ error. */
    [[noreturn]] static void from_python();
};


#define BERTRAND_EXCEPTION(CLS, BASE)                                                   \
    struct CLS : BASE {                                                                 \
    protected:                                                                          \
        explicit constexpr CLS(get_trace trace, std::string_view msg) noexcept :        \
            BASE(trace++, msg)                                                          \
        {}                                                                              \
                                                                                        \
    public:                                                                             \
        [[nodiscard]] virtual std::type_index type() const noexcept override {          \
            return typeid(CLS);                                                         \
        }                                                                               \
        [[noreturn]] virtual void raise() const override { throw *this; }               \
        [[noreturn]] virtual void raise() && override { throw std::move(*this); }       \
                                                                                        \
        [[nodiscard]] explicit constexpr CLS(std::string_view msg = {}) noexcept :      \
            BASE(get_trace{}, msg)                                                      \
        {}                                                                              \
                                                                                        \
        [[nodiscard]] explicit CLS(                                                     \
            cpptrace::raw_trace&& trace,                                                \
            std::string_view msg = {}                                                   \
        ) noexcept :                                                                    \
            BASE(std::move(trace), msg)                                                 \
        {}                                                                              \
                                                                                        \
        [[nodiscard]] explicit CLS(                                                     \
            cpptrace::stacktrace&& trace,                                               \
            std::string_view msg = {}                                                   \
        ) noexcept :                                                                    \
            BASE(std::move(trace), msg)                                                 \
        {}                                                                              \
    };


BERTRAND_EXCEPTION(ArithmeticError, Exception)
    BERTRAND_EXCEPTION(FloatingPointError, ArithmeticError)
    BERTRAND_EXCEPTION(OverflowError, ArithmeticError)
    BERTRAND_EXCEPTION(ZeroDivisionError, ArithmeticError)
BERTRAND_EXCEPTION(AssertionError, Exception)
BERTRAND_EXCEPTION(AttributeError, Exception)
BERTRAND_EXCEPTION(BufferError, Exception)
BERTRAND_EXCEPTION(EOFError, Exception)
BERTRAND_EXCEPTION(ImportError, Exception)
    BERTRAND_EXCEPTION(ModuleNotFoundError, ImportError)
BERTRAND_EXCEPTION(LookupError, Exception)
    BERTRAND_EXCEPTION(IndexError, LookupError)
    BERTRAND_EXCEPTION(KeyError, LookupError)
BERTRAND_EXCEPTION(MemoryError, Exception)
BERTRAND_EXCEPTION(NameError, Exception)
    BERTRAND_EXCEPTION(UnboundLocalError, NameError)
BERTRAND_EXCEPTION(OSError, Exception)
    BERTRAND_EXCEPTION(BlockingIOError, OSError)
    BERTRAND_EXCEPTION(ChildProcessError, OSError)
    BERTRAND_EXCEPTION(ConnectionError, OSError)
        BERTRAND_EXCEPTION(BrokenPipeError, ConnectionError)
        BERTRAND_EXCEPTION(ConnectionAbortedError, ConnectionError)
        BERTRAND_EXCEPTION(ConnectionRefusedError, ConnectionError)
        BERTRAND_EXCEPTION(ConnectionResetError, ConnectionError)
    BERTRAND_EXCEPTION(FileExistsError, OSError)
    BERTRAND_EXCEPTION(FileNotFoundError, OSError)
    BERTRAND_EXCEPTION(InterruptedError, OSError)
    BERTRAND_EXCEPTION(IsADirectoryError, OSError)
    BERTRAND_EXCEPTION(NotADirectoryError, OSError)
    BERTRAND_EXCEPTION(PermissionError, OSError)
    BERTRAND_EXCEPTION(ProcessLookupError, OSError)
    BERTRAND_EXCEPTION(TimeoutError, OSError)
BERTRAND_EXCEPTION(ReferenceError, Exception)
BERTRAND_EXCEPTION(RuntimeError, Exception)
    BERTRAND_EXCEPTION(NotImplementedError, RuntimeError)
    BERTRAND_EXCEPTION(RecursionError, RuntimeError)
BERTRAND_EXCEPTION(StopAsyncIteration, Exception)
BERTRAND_EXCEPTION(StopIteration, Exception)
BERTRAND_EXCEPTION(SyntaxError, Exception)
    BERTRAND_EXCEPTION(IndentationError, SyntaxError)
        BERTRAND_EXCEPTION(TabError, IndentationError)
BERTRAND_EXCEPTION(SystemError, Exception)
BERTRAND_EXCEPTION(TypeError, Exception)
BERTRAND_EXCEPTION(ValueError, Exception)
    BERTRAND_EXCEPTION(UnicodeError, ValueError)
        // BERTRAND_EXCEPTION(UnicodeDecodeError, UnicodeError)
        // BERTRAND_EXCEPTION(UnicodeEncodeError, UnicodeError)
        // BERTRAND_EXCEPTION(UnicodeTranslateError, UnicodeError)


BERTRAND_EXCEPTION(BadUnionAccess, TypeError)


#undef BERTRAND_EXCEPTION


struct UnicodeDecodeError : UnicodeError {
protected:
    explicit constexpr UnicodeDecodeError(
        get_trace trace,
        std::string encoding,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) noexcept :
        UnicodeError(
            trace++,
            "'" + encoding + "' codec can't decode bytes in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        encoding(std::move(encoding)),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}

public:
    [[nodiscard]] virtual std::type_index type() const noexcept override {
        return typeid(UnicodeDecodeError);
    }
    [[noreturn]] virtual void raise() const override { throw *this; }
    [[noreturn]] virtual void raise() && override { throw std::move(*this); }

    std::string encoding;
    std::string object;
    ssize_t start;
    ssize_t end;
    std::string reason;

    [[nodiscard]] explicit constexpr UnicodeDecodeError(
        std::string encoding,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) noexcept :
        UnicodeError(
            get_trace{},
            "'" + encoding + "' codec can't decode bytes in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        encoding(std::move(encoding)),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}

    [[nodiscard]] explicit UnicodeDecodeError(
        cpptrace::raw_trace&& trace,
        std::string encoding,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) noexcept :
        UnicodeError(
            std::move(trace),
            "'" + encoding + "' codec can't decode bytes in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        encoding(std::move(encoding)),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}

    [[nodiscard]] explicit UnicodeDecodeError(
        cpptrace::stacktrace&& trace,
        std::string encoding,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) noexcept :
        UnicodeError(
            std::move(trace),
            "'" + encoding + "' codec can't decode bytes in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        encoding(std::move(encoding)),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}
};


struct UnicodeEncodeError : UnicodeError {
protected:
    explicit constexpr UnicodeEncodeError(
        get_trace trace,
        std::string encoding,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) noexcept :
        UnicodeError(
            trace++,
            "'" + encoding + "' codec can't encode characters in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        encoding(std::move(encoding)),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}

public:
    [[nodiscard]] virtual std::type_index type() const noexcept override {
        return typeid(UnicodeEncodeError);
    }
    [[noreturn]] virtual void raise() const override { throw *this; }
    [[noreturn]] virtual void raise() && override { throw std::move(*this); }

    std::string encoding;
    std::string object;
    ssize_t start;
    ssize_t end;
    std::string reason;

    [[nodiscard]] explicit constexpr UnicodeEncodeError(
        std::string encoding,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) noexcept :
        UnicodeError(
            get_trace{},
            "'" + encoding + "' codec can't encode characters in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        encoding(std::move(encoding)),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}

    [[nodiscard]] explicit UnicodeEncodeError(
        cpptrace::raw_trace&& trace,
        std::string encoding,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) noexcept :
        UnicodeError(
            std::move(trace),
            "'" + encoding + "' codec can't encode characters in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        encoding(std::move(encoding)),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}

    [[nodiscard]] explicit UnicodeEncodeError(
        cpptrace::stacktrace&& trace,
        std::string encoding,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) noexcept :
        UnicodeError(
            std::move(trace),
            "'" + encoding + "' codec can't encode characters in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        encoding(std::move(encoding)),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}
};


struct UnicodeTranslateError : UnicodeError {
protected:
    explicit constexpr UnicodeTranslateError(
        get_trace trace,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) noexcept :
        UnicodeError(
            trace++,
            "can't translate characters in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}

public:
    [[nodiscard]] virtual std::type_index type() const noexcept override {
        return typeid(UnicodeTranslateError);
    }
    [[noreturn]] virtual void raise() const override { throw *this; }
    [[noreturn]] virtual void raise() && override { throw std::move(*this); }

    std::string object;
    ssize_t start;
    ssize_t end;
    std::string reason;

    [[nodiscard]] explicit constexpr UnicodeTranslateError(
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) noexcept :
        UnicodeError(
            get_trace{},
            "can't translate characters in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}

    [[nodiscard]] explicit UnicodeTranslateError(
        cpptrace::raw_trace&& trace,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) noexcept :
        UnicodeError(
            std::move(trace),
            "can't translate characters in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}

    [[nodiscard]] explicit UnicodeTranslateError(
        cpptrace::stacktrace&& trace,
        std::string object,
        ssize_t start,
        ssize_t end,
        std::string reason
    ) noexcept :
        UnicodeError(
            std::move(trace),
            "can't translate characters in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) +
            ": " + reason
        ),
        object(std::move(object)),
        start(start),
        end(end),
        reason(std::move(reason))
    {}
};


}


#endif
