#ifndef BERTRAND_EXCEPT_H
#define BERTRAND_EXCEPT_H


#include <exception>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <variant>

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
    size_t m_skip = 0;
    mutable std::string m_what;
    mutable std::variant<cpptrace::raw_trace, cpptrace::stacktrace> m_trace;

    /* Returns true if the frame originates from an internal context, i.e. from an
    internal C++ library or the Python interpreter. */
    static bool internal_frame(const cpptrace::stacktrace_frame& frame) noexcept {
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
    static bool script_frame(const cpptrace::stacktrace_frame& frame) noexcept {
        return frame.symbol.find("bertrand::Code::operator()") != std::string::npos;
    }

public:

    template <typename Msg = const char*>
        requires (std::constructible_from<std::string, Msg>)
    [[gnu::noinline]] Exception(Msg&& msg = "") :
        m_message(std::forward<Msg>(msg)),
        m_trace(cpptrace::generate_raw_trace(1))
    {}

    template <typename Msg> requires (std::constructible_from<std::string, Msg>)
    Exception(Msg&& msg, cpptrace::raw_trace&& trace) :
        m_message(std::forward<Msg>(msg)),
        m_trace(std::move(trace))
    {}

    template <typename Msg> requires (std::constructible_from<std::string, Msg>)
    Exception(Msg&& msg, cpptrace::stacktrace&& trace) :
        m_message(std::forward<Msg>(msg)),
        m_trace(std::move(trace))
    {}

    /* Skip this frame when reporting the final traceback.  Forwards the exception
    itself for simplified chaining (e.g. `throw exc.skip(2)`). */
    template <typename Self> requires (!meta::is_const<Self>)
    decltype(auto) skip(this Self&& self, size_t count = 1) noexcept {
        self.m_skip += count;
        return std::forward<Self>(self);
    }

    /* Get the current skip depth of the exception traceback. */
    size_t skip_count() const noexcept {
        return m_skip;
    }

    /* The raw text of the exception message, sans exception type and traceback. */
    const char* message() const noexcept {
        return m_message.data();
    }

    /* A resolved trace to the source location where the error occurred.  The trace is
    lazily loaded directly from the program counter when first accessed (typically only
    when an unhandled exception is displayed via the `what()` method). */
    const cpptrace::stacktrace& trace() const noexcept {
        if (cpptrace::raw_trace* trace = std::get_if<cpptrace::raw_trace>(&m_trace)) {
            m_trace.template emplace<cpptrace::stacktrace>(trace->resolve());
        }
        return std::get<cpptrace::stacktrace>(m_trace);
    }

    /* A type index for this exception, which can be searched in the global to_python()
    map to find a corresponding callback. */
    virtual std::type_index type() const noexcept {
        return typeid(Exception);
    }

    /* The plaintext name of the exception type, displayed immediately before the
    error message. */
    virtual const char* name() const noexcept {
        return "Exception";
    }

    /// TODO: py_err<T> will override this again for its use case, where I take a
    /// Python exception and compute a shared traceback from the Python and C++ traces.

    /* The full exception diagnostic, including a coherent, Python-style traceback and
    error text. */
    virtual const char* what() const noexcept override {
        if (m_what.empty()) {
            m_what = "Traceback (most recent call last):\n";
            const cpptrace::stacktrace& trace = this->trace();

            for (size_t i = trace.frames.size(); i-- > m_skip;) {
                const cpptrace::stacktrace_frame& frame = trace.frames[i];
                if (internal_frame(frame)) {
                    continue;
                }
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
            m_what += name();
            m_what += ": ";
            m_what += m_message;
        }
        return m_what.data();
    }

    /* Clear the exception's what() cache, forcing them to be recomputed the next time
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

    /* Throw the exception as a corresponding Python error, pushing it onto the
    active interpreter.  If no callback could be found for this exception (for instance
    if Python isn't loaded), then this will throw a `std::domain_error` in C++
    instead. */
    void to_python() const {
        auto it = impl::exception_to_python.find(type());
        if (it == impl::exception_to_python.end()) {
            throw std::domain_error(
                "no to_python() handler for exception of type '" +
                std::string(name()) + "'"
            );
        }
        it->second(*this);
    }

    /* Catch an exception from Python, re-throwing it as an equivalent C++ error. */
    [[noreturn, clang::noinline]] static void from_python();

};


#define BERTRAND_EXCEPTION(CLS, BASE)                                                   \
    struct CLS : BASE {                                                                 \
        virtual std::type_index type() const noexcept override { return typeid(CLS); }  \
        virtual const char* name() const noexcept override { return #CLS; }             \
                                                                                        \
        template <typename Msg = const char*>                                           \
            requires (std::constructible_from<std::string, Msg>)                        \
        [[gnu::noinline]] CLS(Msg&& msg = "") : BASE(                                   \
            std::forward<Msg>(msg),                                                     \
            cpptrace::generate_raw_trace(1)                                             \
        ) {}                                                                            \
                                                                                        \
        template <typename Msg> requires (std::constructible_from<std::string, Msg>)    \
        CLS(Msg&& msg, cpptrace::raw_trace&& trace) : BASE(                             \
            std::forward<Msg>(msg),                                                     \
            std::move(trace)                                                            \
        ) {}                                                                            \
                                                                                        \
        template <typename Msg> requires (std::constructible_from<std::string, Msg>)    \
        CLS(Msg&& msg, cpptrace::stacktrace&& trace) : BASE(                            \
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
