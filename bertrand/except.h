#ifndef BERTRAND_EXCEPT_H
#define BERTRAND_EXCEPT_H


#include <exception>
#include <string>
#include <typeindex>
#include <unordered_map>

#include <cpptrace/cpptrace.hpp>

#include "bertrand/common.h"


namespace bertrand {


struct Exception;


namespace impl {

    /* This map acts as a late-binding vtable for all bertrand exception types.  It
    holds function pointers that take an arbitrary exception object and construct an
    equivalent Python exception and set it for the active interpreter.  A program is
    malformed and will immediately exit if a subclass of `bertrand::Exception` is
    raised to Python without a corresponding entry in this map. */
    inline std::unordered_map<std::type_index, void(*)(const Exception&)> exception_to_python;

    /// TODO: I would create another exception_from_python map that is keyed on PyObject*
    /// and holds void(*)(PyObject*) functions that take a Python error and throw it as
    /// a proper C++ exception.

}


/* The root of the bertrand exception hierarchy.  This and all its subclasses are
usable just like their built-in Python equivalents, and maintain coherent stack traces
across both languages.  If Python is not loaded, then the same exception types can
still be used in a pure C++ context, but the `from_python()` and `to_python()` helpers
will be disabled. */
struct Exception : public std::exception {
protected:
    std::string m_message;
    mutable size_t m_skip = 0;
    mutable std::string m_what;
    union {
        mutable cpptrace::raw_trace m_raw_trace;
        mutable cpptrace::stacktrace m_stacktrace;
    };
    mutable enum : uint8_t {
        NO_TRACE,
        RAW_TRACE,
        STACK_TRACE
    } m_trace_type = NO_TRACE;

    struct get_trace {
        size_t skip = 1;
        constexpr get_trace operator++(int) const noexcept { return {skip + 1}; }
    };

    /// NOTE: cpptrace always stores the most recent frame first.

    /// TODO: these frame filters might not be necessary if the trim_after() approach
    /// works as intended.

    /* Returns true if the frame originates from an internal context, i.e. from an
    internal C++ library or the Python interpreter. */
    static constexpr bool internal_frame(const cpptrace::stacktrace_frame& frame) noexcept {
        return (
            frame.symbol.starts_with("__") || (
                VIRTUAL_ENV && (
                    frame.filename.starts_with(VIRTUAL_ENV.lib.c_str())
                )
            )
        );
    }

    /* Returns true if the frame indicates the invocation of a Python script from a
    C++ context. */
    static constexpr bool script_frame(const cpptrace::stacktrace_frame& frame) noexcept {
        return frame.symbol.find("bertrand::Code::operator()") != std::string::npos;
    }

    template <typename Msg> requires (std::constructible_from<std::string, Msg>)
    explicit constexpr Exception(Msg&& msg, get_trace trace) :
        m_message(std::forward<Msg>(msg)),
        m_skip(trace.skip + 1)  // skip this constructor
    {
        if !consteval {
            new (&m_raw_trace) cpptrace::raw_trace(cpptrace::generate_raw_trace());
        }
    }

public:

    template <typename Msg = const char*>
        requires (std::constructible_from<std::string, Msg>)
    explicit constexpr Exception(Msg&& msg = "") :
        m_message(std::forward<Msg>(msg)),
        m_skip(1)  // skip this constructor
    {
        if !consteval {
            new (&m_raw_trace) cpptrace::raw_trace(cpptrace::generate_raw_trace());
        }
    }

    template <typename Msg> requires (std::constructible_from<std::string, Msg>)
    explicit Exception(Msg&& msg, cpptrace::raw_trace&& trace) :
        m_message(std::forward<Msg>(msg)),
        m_trace_type(RAW_TRACE)
    {
        new (&m_raw_trace) cpptrace::raw_trace(std::move(trace));
    }

    template <typename Msg> requires (std::constructible_from<std::string, Msg>)
    explicit Exception(Msg&& msg, cpptrace::stacktrace&& trace) :
        m_message(std::forward<Msg>(msg)),
        m_trace_type(STACK_TRACE)
    {
        new (&m_stacktrace) cpptrace::stacktrace(std::move(trace));
    }

    Exception(const Exception& other) :
        m_message(other.m_message),
        m_skip(other.m_skip),
        m_what(other.m_what),
        m_trace_type(other.m_trace_type)
    {
        if !consteval {
            switch (m_trace_type) {
                case RAW_TRACE:
                    new (&m_raw_trace) cpptrace::raw_trace(other.m_raw_trace);
                    break;
                case STACK_TRACE:
                    new (&m_stacktrace) cpptrace::stacktrace(other.m_stacktrace);
                    break;
                default:
                    break;
            }
        }
    }

    Exception(Exception&& other) :
        m_message(std::move(other.m_message)),
        m_skip(other.m_skip),
        m_what(std::move(other.m_what)),
        m_trace_type(other.m_trace_type)
    {
        if !consteval {
            switch (m_trace_type) {
                case RAW_TRACE:
                    new (&m_raw_trace) cpptrace::raw_trace(std::move(other.m_raw_trace));
                    break;
                case STACK_TRACE:
                    new (&m_stacktrace) cpptrace::stacktrace(std::move(other.m_stacktrace));
                    break;
                default:
                    break;
            }
        }
    }

    Exception& operator=(const Exception& other) {
        if (&other != this) {
            m_message = other.m_message;
            m_skip = other.m_skip;
            m_what = other.m_what;
            m_trace_type = other.m_trace_type;
            if !consteval {
                switch (m_trace_type) {
                    case RAW_TRACE:
                        new (&m_raw_trace) cpptrace::raw_trace(other.m_raw_trace);
                        break;
                    case STACK_TRACE:
                        new (&m_stacktrace) cpptrace::stacktrace(other.m_stacktrace);
                        break;
                    default:
                        break;
                }
            }
        }
        return *this;
    }

    Exception& operator=(Exception&& other) {
        if (&other != this) {
            m_message = std::move(other.m_message);
            m_skip = other.m_skip;
            m_what = std::move(other.m_what);
            m_trace_type = other.m_trace_type;
            if !consteval {
                switch (m_trace_type) {
                    case RAW_TRACE:
                        new (&m_raw_trace) cpptrace::raw_trace(std::move(other.m_raw_trace));
                        break;
                    case STACK_TRACE:
                        new (&m_stacktrace) cpptrace::stacktrace(std::move(other.m_stacktrace));
                        break;
                    default:
                        break;
                }
            }
        }
        return *this;
    }

    ~Exception() noexcept {
        if !consteval {
            switch (m_trace_type) {
                case RAW_TRACE:
                    m_raw_trace.~raw_trace();
                    break;
                case STACK_TRACE:
                    m_stacktrace.~stacktrace();
                    break;
                default:
                    break;
            }
        }
    }

    /* Skip the `n` most recent frames in the stack trace.  Note that this works by
    incrementing an internal counter, so no extra traces are resolved at runtime, and
    it is not guaranteed that the first skipped frame is the current one, unless all
    earlier frames have been already been skipped in a similar fashion.  Forwards the
    exception itself for simplified chaining (e.g. `throw exc.skip(2)`). */
    template <typename Self>
    constexpr decltype(auto) skip(this Self&& self, size_t n = 0) noexcept {
        if !consteval {
            ++n;  // always skip this method
            if (self.m_trace_type == STACK_TRACE) {
                if (n >= self.m_stacktrace.frames.size()) {
                    self.m_stacktrace.frames.clear();
                } else {
                    self.m_stacktrace.frames.erase(
                        self.m_stacktrace.frames.begin(),
                        self.m_stacktrace.frames.begin() + n
                    );
                }
            }
            self.m_skip += n;
        }
        return std::forward<Self>(self);
    }

    /* Discard any frames that are more recent than the frame in which this method was
    invoked, or an earlier frame if an offset is supplied.  Forwards the exception
    itself for simplified chaining (e.g. `throw exc.trim_before()`), and also resets
    the `skip()` counter to start counting from the current frame. */
    template <typename Self>
    constexpr decltype(auto) trim_before(this Self&& self, size_t offset = 0) noexcept {
        if !consteval {
            ++offset;  // always skip this method
            cpptrace::raw_trace curr = cpptrace::generate_raw_trace();
            if (offset > curr.frames.size()) {
                return std::forward<Self>(self);  // no frames to cut
            }
            cpptrace::frame_ptr pivot = curr.frames[curr.frames.size() - offset];
            switch (self.m_trace_type) {
                case RAW_TRACE:
                    for (size_t i = self.m_raw_trace.frames.size(); i-- > self.m_skip;) {
                        if (self.m_raw_trace.frames[i] == pivot) {
                            self.m_raw_trace.frames.erase(
                                self.m_raw_trace.frames.begin(),
                                self.m_raw_trace.frames.begin() + i
                            );
                            self.m_skip = 0;
                            return std::forward<Self>(self);
                        }
                    }
                    break;
                case STACK_TRACE:
                    for (size_t i = self.m_stacktrace.frames.size(); i-- > self.m_skip;) {
                        if (self.m_stacktrace.frames[i].raw_address == pivot) {
                            self.m_stacktrace.frames.erase(
                                self.m_stacktrace.frames.begin(),
                                self.m_stacktrace.frames.begin() + i
                            );
                            self.m_skip = 0;
                            return std::forward<Self>(self);
                        }
                    }
                    break;
                default:
                    break;
            }
        }
        return std::forward<Self>(self);
    }

    /* Discard any frames that are less recent than the frame in which this method was
    invoked, or a later frame if an offset is supplied.  Forwards the exception
    itself for simplified chaining (e.g. `throw exc.trim_after()`) */
    template <typename Self>
    constexpr decltype(auto) trim_after(this Self&& self, size_t offset = 0) noexcept {
        if !consteval {
            ++offset;  // always skip this method
            cpptrace::raw_trace curr = cpptrace::generate_raw_trace();
            if (offset > curr.frames.size()) {
                return std::forward<Self>(self);  // no frames to cut
            }
            cpptrace::frame_ptr pivot = curr.frames[offset];
            switch (self.m_trace_type) {
                case RAW_TRACE:
                    for (size_t i = self.m_skip; i < self.m_raw_trace.frames.size(); ++i) {
                        if (self.m_raw_trace.frames[i] == pivot) {
                            self.m_raw_trace.frames.resize(i + 1);
                            return std::forward<Self>(self);
                        }
                    }
                    break;
                case STACK_TRACE:
                    for (size_t i = self.m_skip; i < self.m_stacktrace.frames.size(); ++i) {
                        if (self.m_stacktrace.frames[i].raw_address == pivot) {
                            self.m_stacktrace.frames.resize(i + 1);
                            return std::forward<Self>(self);
                        }
                    }
                    break;
                default:
                    break;
            }
        }
        return std::forward<Self>(self);
    }

    /* The raw text of the exception message, sans exception type and traceback. */
    constexpr const char* message() const noexcept {
        return m_message.data();
    }

    /* A resolved trace to the source location where the error occurred, with internal
    C++/Python frames removed.  The trace is lazily loaded directly from the program
    counter when first accessed (typically only when an unhandled exception is
    displayed via the `what()` method).  This may return a null pointer if the
    exception has no traceback to report, which only occurs when an exception is thrown
    in a constexpr context (C++26 and later). */
    constexpr const cpptrace::stacktrace* trace() const noexcept {
        if !consteval {
            if (m_trace_type == STACK_TRACE) {
                return &m_stacktrace;
            } else if (m_trace_type == RAW_TRACE) {
                cpptrace::stacktrace trace = m_raw_trace.resolve();
                cpptrace::stacktrace filtered;
                if (m_skip < trace.frames.size()) {
                    filtered.frames.reserve(trace.frames.size() - m_skip);
                }
                for (size_t i = m_skip; i < trace.frames.size(); ++i) {
                    cpptrace::stacktrace_frame& frame = trace.frames[i];
                    if constexpr (!DEBUG) {
                        if (frame.symbol.starts_with("__")) {
                            continue;  // filter out C++ internals in release mode
                        }
                    }
                    filtered.frames.emplace_back(std::move(frame));
                }
                m_raw_trace.~raw_trace();
                new (&m_stacktrace) cpptrace::stacktrace(std::move(filtered));
                m_trace_type = STACK_TRACE;
                return &m_stacktrace;
            }
        }
        return nullptr;
    }

    /* A type index for this exception, which can be searched in the global to_python()
    map to find a corresponding callback. */
    virtual std::type_index type() const noexcept {
        return typeid(Exception);
    }

    /* The plaintext name of the exception type, displayed immediately before the
    error message. */
    constexpr virtual const char* name() const noexcept {
        return "Exception";
    }

    /// TODO: py_err<T> will override this again for its use case, where I take a
    /// Python exception and compute a shared traceback from the Python and C++ traces.

    /* The full exception diagnostic, including a coherent, Python-style traceback and
    error text. */
    constexpr virtual const char* what() const noexcept override {
        if (m_what.empty()) {
            if (const cpptrace::stacktrace* trace = this->trace()) {
                m_what = "Traceback (most recent call last):\n";
                for (const cpptrace::stacktrace_frame& frame : trace->frames) {
                    m_what += "    File \"" + frame.filename + "\", line ";
                    if (frame.line.has_value()) {
                        m_what += std::to_string(frame.line.value()) + ", in ";
                    } else {
                        m_what += "<unknown>, in ";
                    }
                    if (frame.is_inline) {
                        m_what += "[inline] ";
                    }
                    m_what += frame.symbol + "\n";
                }
            }
            m_what += name();
            m_what += ": ";
            m_what += m_message;
        }
        return m_what.data();
    }

    /* Clear the exception's what() cache, forcing it to be recomputed the next time
    it is requested. */
    void flush() noexcept {
        m_what.clear();
    }

    /// TODO: to_python() will also filter the cpptrace error to stop at the last
    /// bertrand::Code::operator() call, similar to what I'm currently doing.  If
    /// the error was already caught from python, then it will already have a coherent
    /// error trace, and all I have to do is paste the C++ trace to the end of it,
    /// starting from the most recent frame.
    /// -> Actually the bertrand::Code::operator() filtering only needs to be done
    /// if I'm catching a C++ exception from Python?

    /* Throw the most recent C++ exception as a corresponding Python error, pushing it
    onto the active interpreter.  If there is no unhandled exception for this thread or
    no callback could be found (for instance if Python isn't loaded), then this will
    terminate the program instead. */
    static void to_python() noexcept {
        try {
            throw;

        } catch (const Exception& exc) {
            auto it = impl::exception_to_python.find(exc.type());
            if (it == impl::exception_to_python.end()) {
                std::cerr <<
                    "no to_python() handler for exception of type '" +
                    std::string(exc.name()) + "'";
                std::exit(1);
            }
            it->second(exc.trim_after(1));  // terminate at caller

        } catch (const std::exception& e) {
            Exception exc(e.what());
            auto it = impl::exception_to_python.find(exc.type());
            if (it == impl::exception_to_python.end()) {
                std::cerr <<
                    "no to_python() handler for exception of type '" << exc.name() << "'";
                std::exit(1);
            }
            it->second(exc.trim_after(1));  // terminate at caller

        } catch (...) {
            Exception exc("unknown C++ exception");
            auto it = impl::exception_to_python.find(exc.type());
            if (it == impl::exception_to_python.end()) {
                std::cerr <<
                    "no to_python() handler for exception of type '" << exc.name() << "'";
                std::exit(1);
            }
            it->second(exc.trim_after(1));  // terminate at caller
        }
    }

    /* Catch an exception from Python, re-throwing it as an equivalent C++ error. */
    [[noreturn]] static void from_python();

};


#define BERTRAND_EXCEPTION(CLS, BASE)                                                   \
    struct CLS : BASE {                                                                 \
    protected:                                                                          \
                                                                                        \
        template <typename Msg> requires (std::constructible_from<std::string, Msg>)    \
        explicit constexpr CLS(Msg&& msg, get_trace trace) : BASE(                      \
            std::forward<Msg>(msg),                                                     \
            trace++                                                                     \
        ) {}                                                                            \
                                                                                        \
    public:                                                                             \
        virtual std::type_index type() const noexcept override { return typeid(CLS); }  \
        constexpr virtual const char* name() const noexcept override { return #CLS; }   \
                                                                                        \
        template <typename Msg = const char*>                                           \
            requires (std::constructible_from<std::string, Msg>)                        \
        explicit constexpr CLS(Msg&& msg = "") : BASE(                                  \
            std::forward<Msg>(msg),                                                     \
            get_trace{}                                                                 \
        ) {}                                                                            \
                                                                                        \
        template <typename Msg> requires (std::constructible_from<std::string, Msg>)    \
        explicit CLS(Msg&& msg, cpptrace::raw_trace&& trace) : BASE(                    \
            std::forward<Msg>(msg),                                                     \
            std::move(trace)                                                            \
        ) {}                                                                            \
                                                                                        \
        template <typename Msg> requires (std::constructible_from<std::string, Msg>)    \
        explicit CLS(Msg&& msg, cpptrace::stacktrace&& trace) : BASE(                   \
            std::forward<Msg>(msg),                                                     \
            std::move(trace)                                                            \
        ) {}                                                                            \
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


/// TODO: Unicode errors have special cases for the constructor, which may need to
/// be accounted for.


#undef BERTRAND_EXCEPTION


}  // namespace bertrand


#endif  // BERTRAND_EXCEPT_H
