#if !defined(BERTRAND_PYTHON_COMMON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/common.h> instead."
#endif

#ifndef BERTRAND_PYTHON_COMMON_EXCEPTIONS_H
#define BERTRAND_PYTHON_COMMON_EXCEPTIONS_H

#include "declarations.h"


/* CPython exception types:
 *      https://docs.python.org/3/c-api/exceptions.html#standard-exceptions
 *
 * Inheritance hierarchy:
 *      https://docs.python.org/3/library/exceptions.html#exception-hierarchy
 */


namespace bertrand {
namespace py {


namespace impl {

    /* A language-agnostic stack frame that is used when reporting mixed Python/C++
    error tracebacks. */
    class StackFrame {
        mutable std::string string;
        mutable PyFrameObject* py_frame = nullptr;
        PyThreadState* thread;

    public:
        std::string filename;
        std::string funcname;
        int lineno = 0;
        bool is_inline = false;

        StackFrame(PyFrameObject* frame = nullptr, PyThreadState* tstate = nullptr) :
            py_frame(frame),
            thread(tstate == nullptr ? PyThreadState_Get() : tstate)
        {
            if (py_frame != nullptr) {
                #if (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 9)
                    PyCodeObject* code = PyFrame_GetCode(py_frame);
                #else
                    PyCodeObject* code = reinterpret_cast<PyCodeObject*>(
                        Py_XNewRef(py_frame->f_code)
                    );
                #endif

                if (code != nullptr) {
                    filename = PyUnicode_AsUTF8(code->co_filename);
                    funcname = PyUnicode_AsUTF8(code->co_name);
                    Py_DECREF(code);
                }
                lineno = PyFrame_GetLineNumber(frame);
            }
        }

        StackFrame(
            const cpptrace::stacktrace_frame& frame,
            PyThreadState* tstate = nullptr
        ) :
            thread(tstate == nullptr ? PyThreadState_Get() : tstate),
            filename(frame.filename),
            funcname(frame.symbol),
            lineno(frame.line.value_or(0)),
            is_inline(frame.is_inline)
        {}

        StackFrame(
            const std::string& filename,
            const std::string& funcname,
            int lineno,
            bool is_inline = false,
            PyThreadState* tstate = nullptr
        ) :
            thread(tstate == nullptr ? PyThreadState_Get() : tstate),
            filename(filename),
            funcname(funcname),
            lineno(lineno),
            is_inline(is_inline)
        {}

        StackFrame(const StackFrame& other) :
            string(other.string),
            py_frame(reinterpret_cast<PyFrameObject*>(Py_XNewRef(other.py_frame))),
            thread(other.thread),
            filename(other.filename),
            funcname(other.funcname),
            lineno(other.lineno),
            is_inline(other.is_inline)
        {}

        StackFrame(StackFrame&& other) :
            string(std::move(other.string)),
            py_frame(other.py_frame),
            thread(other.thread),
            filename(std::move(other.filename)),
            funcname(std::move(other.funcname)),
            lineno(other.lineno),
            is_inline(other.is_inline)
        {
            other.py_frame = nullptr;
            other.thread = nullptr;
        }

        StackFrame& operator=(const StackFrame& other) {
            if (&other != this) {
                PyFrameObject* old_frame = py_frame;
                string = other.string;
                thread = other.thread;
                py_frame = reinterpret_cast<PyFrameObject*>(Py_XNewRef(other.py_frame));
                filename = other.filename;
                funcname = other.funcname;
                lineno = other.lineno;
                is_inline = other.is_inline;
                Py_XDECREF(old_frame);
            }
            return *this;
        }

        StackFrame& operator=(StackFrame&& other) {
            if (&other != this) {
                PyFrameObject* old_frame = py_frame;
                string = std::move(other.string);
                thread = other.thread;
                py_frame = other.py_frame;
                filename = std::move(other.filename);
                funcname = std::move(other.funcname);
                lineno = other.lineno;
                is_inline = other.is_inline;
                other.py_frame = nullptr;
                other.thread = nullptr;
                Py_XDECREF(old_frame);
            }
            return *this;
        }

        ~StackFrame() noexcept {
            Py_XDECREF(py_frame);
        }

        /* Convert this stack frame into an empty Python frame object, which is
        cached. */
        PyFrameObject* to_python() const {
            if (py_frame == nullptr) {
                PyObject* globals = PyDict_New();
                if (globals == nullptr) {
                    throw std::runtime_error(
                        "could not convert StackFrame into Python frame object - "
                        "failed to create globals dictionary"
                    );
                }
                PyCodeObject* code = PyCode_NewEmpty(
                    filename.c_str(),
                    funcname.c_str(),
                    lineno
                );
                if (code == nullptr) {
                    Py_DECREF(globals);
                    throw std::runtime_error(
                        "could not convert StackFrame into Python frame object - "
                        "failed to create code object"
                    );
                }
                py_frame = PyFrame_New(thread, code, globals, nullptr);
                Py_DECREF(globals);
                Py_DECREF(code);
                if (py_frame == nullptr) {
                    throw std::runtime_error(
                        "Error when converting StackFrame into Python frame object - "
                        "failed to initialize empty frame"
                    );
                }
                py_frame->f_lineno = lineno;
            }
            return py_frame;
        }

        /* Convert this stack frame into a string representation, for use in exception
        tracebacks. */
        const std::string& to_string() const noexcept {
            if (string.empty()) {
                string = "File \"" + filename + "\", line ";
                string += std::to_string(lineno) + ", in ";
                if (is_inline) {
                    string += "[inline] ";
                }
                string += funcname;
            }
            return string;
        }

        /* Stream the stack frame into an output stream. */
        friend std::ostream& operator<<(std::ostream& os, const StackFrame& self) noexcept {
            const std::string& str = self.to_string();
            os.write(str.c_str(), str.size());
            return os;
        }

    };

    /* A language-agnostic stack trace that is attached to all Python/C++ errors. */
    class StackTrace {
        mutable std::string string;
        mutable PyTracebackObject* py_traceback = nullptr;
        PyThreadState* thread;

        /* Return true if a C++ stack frame does not refer to an internal frame within
        the Python interpreter, pybind11 bindings, or the C++ standard library. */
        static bool ignore(const cpptrace::stacktrace_frame& frame) {
            return (
                frame.filename.find("usr/bin/python") != std::string::npos ||
                frame.symbol.find("pybind11::cpp_function::") != std::string::npos ||
                frame.symbol.starts_with("__")
            );
        }

    public:
        // NOTE: the stack is stored in proper execution order
        // (i.e. [head] most recent -> least recent [tail]).  This is reversed from
        // both Python and cpptrace, which report tracebacks in the opposite order.
        std::deque<StackFrame> stack;

        BERTRAND_NOINLINE explicit StackTrace(
            size_t skip = 0,
            PyThreadState* tstate = nullptr
        ) : StackTrace(cpptrace::generate_trace(++skip), tstate)
        {}

        explicit StackTrace(
            const cpptrace::stacktrace& stacktrace,
            PyThreadState* tstate = nullptr
        ) : thread(tstate == nullptr ? PyThreadState_Get() : tstate)
        {
            for (auto&& frame : stacktrace) {
                if (frame.symbol.find("py::Code::operator()") != std::string::npos) {
                    break;
                } else if (!ignore(frame)) {
                    stack.emplace_front(frame, thread);
                }
            }
        }

        explicit StackTrace(
            PyTracebackObject* traceback,
            const cpptrace::stacktrace& stacktrace,
            PyThreadState* tstate = nullptr
        ) : thread(tstate == nullptr ? PyThreadState_Get() : tstate)
        {
            if (traceback == nullptr) {
                #if (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12)
                    if (thread->current_exception != nullptr) {
                        traceback = reinterpret_cast<PyTracebackObject*>(
                            PyException_GetTraceback(thread->current_exception)
                        );
                    }
                #else
                    traceback = reinterpret_cast<PyTracebackObject*>(
                        Py_XNewRef(thread->curexc_traceback)
                    );
                    if (traceback == nullptr && thread->curexc_value != nullptr) {
                        traceback = reinterpret_cast<PyTracebackObject*>(
                            PyException_GetTraceback(thread->curexc_value)
                        );
                    }
                #endif
            }

            // Python tracebacks are stored least recent -> most recent, so we can
            // insert them in the same order.
            while (traceback != nullptr) {
                stack.emplace_back(traceback->tb_frame, thread);
                traceback = traceback->tb_next;
            }

            // C++ tracebacks are stored most recent -> least recent, so we need to
            // reverse them during construction.  Since the Python frames are considered
            // to be more recent than the C++ frames in this context, we prepend to the
            // stack to obtain the proper execution order (from C++ -> Python).
            for (const cpptrace::stacktrace_frame& frame : stacktrace) {
                if (frame.symbol.find("py::Code::operator()") != std::string::npos) {
                    break;
                } else if (!ignore(frame)) {
                    stack.emplace_front(frame, thread);
                }
            }
        }

        StackTrace(
            std::deque<StackFrame>&& stack,
            PyThreadState* tstate = nullptr
        ) : thread(tstate == nullptr ? PyThreadState_Get() : tstate),
            stack(std::move(stack))
        {}

        StackTrace(
            const std::deque<StackFrame>& stack,
            PyThreadState* tstate = nullptr
        ) : thread(tstate == nullptr ? PyThreadState_Get() : tstate),
            stack(stack)
        {}

        StackTrace(const StackTrace& other) :
            string(other.string),
            py_traceback(reinterpret_cast<PyTracebackObject*>(
                Py_XNewRef(other.py_traceback))
            ),
            thread(other.thread),
            stack(other.stack)
        {}

        StackTrace(StackTrace&& other) :
            string(std::move(other.string)),
            py_traceback(other.py_traceback),
            thread(other.thread),
            stack(std::move(other.stack))
        {
            other.py_traceback = nullptr;
            other.thread = nullptr;
        }

        StackTrace& operator=(const StackTrace& other) {
            if (&other != this) {
                PyTracebackObject* old_traceback = py_traceback;
                string = other.string;
                py_traceback = reinterpret_cast<PyTracebackObject*>(
                    Py_XNewRef(other.py_traceback)
                );
                thread = other.thread;
                stack = other.stack;
                Py_XDECREF(old_traceback);
            }
            return *this;
        }

        StackTrace& operator=(StackTrace&& other) {
            if (&other != this) {
                PyTracebackObject* old_traceback = py_traceback;
                string = std::move(other.string);
                py_traceback = other.py_traceback;
                thread = other.thread;
                stack = std::move(other.stack);
                other.py_traceback = nullptr;
                other.thread = nullptr;
                Py_XDECREF(old_traceback);
            }
            return *this;
        }

        ~StackTrace() noexcept {
            Py_XDECREF(py_traceback);
        }

        size_t size() const noexcept { return stack.size(); }
        auto begin() const noexcept { return stack.begin(); }
        auto end() const noexcept { return stack.end(); }
        auto rbegin() const noexcept { return stack.rbegin(); }
        auto rend() const noexcept { return stack.rend(); }

        /* Set an active Python error with this traceback. */
        void restore(PyObject* type, const char* value) const {
            PyTracebackObject* tb = to_python();
            PyErr_SetString(type, value);
            #if (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12)
                if (tb != nullptr) {
                    PyException_SetTraceback(
                        thread->current_exception,
                        reinterpret_cast<PyObject*>(tb)
                    );
                }
            #else
                thread->curexc_traceback = Py_XNewRef(tb);
            #endif
        }

        /* Build an equivalent Python traceback object for this stack trace.  The
        result is cached and reused on subsequent calls. */
        PyTracebackObject* to_python() const {
            if (py_traceback == nullptr && !stack.empty()) {
                auto it = stack.rbegin();
                auto end = stack.rend();
                while (it != end) {
                    PyTracebackObject* tb = PyObject_GC_New(
                        PyTracebackObject,
                        &PyTraceBack_Type
                    );
                    if (tb == nullptr) {
                        throw std::runtime_error(
                            "could not create Python traceback object - failed to allocate "
                            "PyTraceBackObject"
                        );
                    }
                    tb->tb_next = py_traceback;
                    tb->tb_frame = reinterpret_cast<PyFrameObject*>(
                        Py_NewRef((*it).to_python())
                    );
                    #if (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 11)
                        tb->tb_lasti = PyFrame_GetLasti(tb->tb_frame) * sizeof(_Py_CODEUNIT);
                    #else
                        tb->tb_lasti = tb->tb_frame->f_lasti * sizeof(_Py_CODEUNIT);
                    #endif
                    tb->tb_lineno = PyFrame_GetLineNumber(tb->tb_frame);
                    PyObject_GC_Track(tb);
                    py_traceback = tb;
                    ++it;
                }
            }
            return py_traceback;
        }

        /* Force a rebuild of the Python traceback the next time `to_python()` is
        called. */
        void flush_python() noexcept {
            PyTracebackObject* tb = py_traceback;
            py_traceback = nullptr;
            Py_XDECREF(tb);
        }

        /* Convert the traceback into a string representation, for use in C++ error
        messages.  These mimic the Python style even in pure C++ contexts. */
        const std::string& to_string() const noexcept {
            if (string.empty()) {
                string = "Traceback (most recent call last):";
                for (auto&& frame : stack) {
                    string += "\n  " + frame.to_string();
                }
            }
            return string;
        }

        /* Force a rebuild of the C++ traceback the next time `what()` is called. */
        void flush_string() noexcept {
            string = "";
        }

        /* Check whether the traceback has any entries. */
        explicit operator bool() const noexcept {
            return !stack.empty();
        }

        /* Stream the traceback into an output stream. */
        friend std::ostream& operator<<(std::ostream& os, const StackTrace& self) noexcept {
            const std::string& str = self.to_string();
            os.write(str.c_str(), str.size());
            return os;
        }

    };

    /* A map that allows Python exception types to be perfectly reflected in C++
    try/catch semantics.  This is automatically populated by the BERTRAND_EXCEPTION()
    macro, and is only used when catching indeterminate Python errors from C++.  */
    static std::unordered_map<
        PyObject*,
        std::function<void(PyObject*, PyObject*, PyObject*, size_t, PyThreadState*)>
    > rethrow_exception_map;

    /* Register a new exception type, populating it in the exception map.  This is
    automatically called by the BERTRAND_EXCEPTION() macro, so that users should never
    need to call it themselves. */
    template <typename cpp_type>
    bool register_exception(PyObject* py_type) {
        auto it = rethrow_exception_map.find(py_type);
        if (it == rethrow_exception_map.end()) {
            rethrow_exception_map[py_type] = [](
                PyObject* type,
                PyObject* value,
                PyObject* traceback,
                size_t skip,
                PyThreadState* thread
            ) {
                throw cpp_type(
                    type,
                    value,
                    traceback,
                    ++skip,
                    thread
                );
            };
        }
        return true;
    }

    #define BERTRAND_EXCEPTION(cls, base, pytype)                                       \
        static_assert(                                                                  \
            std::is_base_of_v<Exception, base>,                                         \
            "exception base class must derive from py::Exception"                       \
        );                                                                              \
        static_assert(                                                                  \
            std::is_same_v<PyObject*, decltype(pytype)>,                                \
            "exception type must be a PyObject* pointer"                                \
        );                                                                              \
                                                                                        \
        class PYBIND11_EXPORT_EXCEPTION cls : public base {                             \
            static bool registered;                                                     \
                                                                                        \
            template <typename cpp_type>                                                \
            friend bool register_exception(PyObject* py_type);                          \
                                                                                        \
        public:                                                                         \
            using base::base;                                                           \
                                                                                        \
            BERTRAND_NOINLINE explicit cls(                                             \
                const std::string& message = "",                                        \
                size_t skip = 0,                                                        \
                PyThreadState* thread = nullptr                                         \
            ) : base(message, get_trace(skip), thread)                                  \
            {}                                                                          \
                                                                                        \
            BERTRAND_NOINLINE explicit cls(                                             \
                const std::string& message,                                             \
                const cpptrace::stacktrace& trace,                                      \
                PyThreadState* thread = nullptr                                         \
            ) : base(message, trace, thread)                                            \
            {}                                                                          \
                                                                                        \
            BERTRAND_NOINLINE explicit cls(                                             \
                PyObject* type,                                                         \
                PyObject* value,                                                        \
                PyObject* traceback,                                                    \
                size_t skip = 0,                                                        \
                PyThreadState* thread = nullptr                                         \
            ) : base(type, value, traceback, get_trace(skip), thread)                   \
            {}                                                                          \
                                                                                        \
            BERTRAND_NOINLINE explicit cls(                                             \
                PyObject* type,                                                         \
                PyObject* value,                                                        \
                PyObject* traceback,                                                    \
                const cpptrace::stacktrace& trace,                                      \
                PyThreadState* thread = nullptr                                         \
            ) : base(type, value, traceback, trace, thread)                             \
            {}                                                                          \
                                                                                        \
            virtual const char* what() const noexcept override {                        \
                if (what_string.empty()) {                                              \
                    what_string += traceback.to_string();                               \
                    what_string += "\n"#cls": ";                                        \
                    what_string += message();                                           \
                }                                                                       \
                return what_string.c_str();                                             \
            }                                                                           \
                                                                                        \
            virtual void set_error() const override {                                   \
                traceback.restore(pytype, message());                                   \
            }                                                                           \
                                                                                        \
            static void from_python(                                                    \
                size_t skip = 0,                                                        \
                PyThreadState* thread = nullptr                                         \
            ) = delete;                                                                 \
                                                                                        \
            static void from_pybind11(                                                  \
                size_t skip = 0,                                                        \
                PyThreadState* thread = nullptr                                         \
            ) = delete;                                                                 \
        };                                                                              \
                                                                                        \
        bool cls::registered = impl::register_exception<cls>(pytype);                   \

}


/* Base exception class.  Appends a C++ stack trace that will be propagated up to
Python for cross-language diagnostics. */
class Exception : public pybind11::builtin_exception {
    using Base = pybind11::builtin_exception;
    static bool registered;

    template <typename cpp_type>
    friend bool register_exception(PyObject* py_type);

protected:
    mutable std::string what_string;

    static std::string parse_value(PyObject* obj) {
        PyObject* string = PyObject_Str(obj);
        if (string == nullptr) {
            throw std::runtime_error(
                "could not convert Python exception into a C++ exception - "
                "str(exception) is ill-formed"
            );
        }
        Py_ssize_t size;
        const char* data = PyUnicode_AsUTF8AndSize(string, &size);
        if (data == nullptr) {
            Py_DECREF(string);
            throw std::runtime_error(
                "could not convert Python exception into a C++ exception - "
                "str(exception) is not a valid UTF-8 string"
            );
        }
        std::string result(data, size);
        Py_DECREF(string);
        return result;
    }

    /* Protected method gets a C++ stack trace to a particular context without going
    through inherited constructors. */
    static cpptrace::stacktrace get_trace(size_t skip) {
        auto result = cpptrace::generate_trace(++skip);
        return result;
    }

public:
    impl::StackTrace traceback;

    BERTRAND_NOINLINE explicit Exception(
        const std::string& message = "",
        size_t skip = 0,
        PyThreadState* thread = nullptr
    ) : Base(message), traceback(get_trace(skip), thread)
    {}

    BERTRAND_NOINLINE explicit Exception(
        const std::string& message,
        const cpptrace::stacktrace& trace,
        PyThreadState* thread = nullptr
    ) : Base(message), traceback(trace, thread)
    {}

    BERTRAND_NOINLINE explicit Exception(
        PyObject* type,
        PyObject* value,
        PyObject* traceback,
        size_t skip = 0,
        PyThreadState* thread = nullptr
    ) : Base(value == nullptr ? std::string() : parse_value(value)),
        traceback(
            reinterpret_cast<PyTracebackObject*>(traceback),
            get_trace(skip),
            thread
        )
    {
        if (type == nullptr) {
            throw std::logic_error(
                "could not convert Python exception into a C++ exception - exception "
                "type is not set."
            );
        }
    }

    BERTRAND_NOINLINE explicit Exception(
        PyObject* type,
        PyObject* value,
        PyObject* traceback,
        const cpptrace::stacktrace& trace,
        PyThreadState* thread = nullptr
    ) : Base(value == nullptr ? std::string() : parse_value(value)),
        traceback(
            reinterpret_cast<PyTracebackObject*>(traceback),
            trace,
            thread
        )
    {
        if (type == nullptr) {
            throw std::logic_error(
                "could not convert Python exception into a C++ exception - exception "
                "type is not set."
            );
        }
    }

    Exception(const Exception& other) :
        Base(other), what_string(other.what_string), traceback(other.traceback)
    {}

    Exception& operator=(const Exception& other) {
        if (&other != this) {
            Base::operator=(other);
            what_string = other.what_string;
            traceback = other.traceback;
        }
        return *this;
    }

    /* Get just the message that is associated with this error.  This corresponds to
    the what() message of a typical C++ error, without the Python-style traceback and
    error type. */
    const char* message() const noexcept {
        return Base::what();
    }

    /* Generate the message that will be printed if this error is propagated to a C++
    context without being explicitly caught.  If debug symbols are enabled, then this
    will include a Python-style traceback covering both the Python and C++ frames that
    were traversed to reach the error. */
    virtual const char* what() const noexcept override {
        // TODO: check for debug symbols before generating traceback
        if (what_string.empty()) {
            what_string += traceback.to_string();
            what_string += "\nException: ";
            what_string += message();
        }
        return what_string.c_str();
    }

    /* Convert this exception into an equivalent Python error, so that it can be
    propagated to a Python context.  The resulting traceback reflects both the Python
    and C++ frames that were traversed to reach the error.  */
    virtual void set_error() const override {
        traceback.restore(PyExc_Exception, message());
    }

    /* Retrieve an error from a Python context and re-throw it as a C++ error with a
    matching type.  This effectively replaces `pybind11::error_already_set()` and
    removes the special case that it represents during try/catch blocks.  Note that
    this is a void function that always throws. */
    [[noreturn]] BERTRAND_NOINLINE static void from_python(
        size_t skip = 0,
        PyThreadState* thread = nullptr
    ) {
        if (thread == nullptr) {
            thread = PyThreadState_Get();
        }

        // interacting with the Python error state is rather clumsy and was recently
        // changed in Python 3.12, so we need to handle both cases
        #if (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12)
            PyObject* value = thread->current_exception;
            if (value == nullptr) {
                throw std::logic_error(
                    "could not convert Python exception into a C++ exception - "
                    "exception is not set."
                );
            }
            PyObject* type = Py_NewRef(Py_TYPE(value));
            PyObject* traceback = PyException_GetTraceback(value);  // new ref
            thread->current_exception = nullptr;
        #else
            PyObject* type = thread->curexc_type;
            PyObject* value = thread->curexc_value;
            PyObject* traceback = Py_XNewRef(thread->curexc_traceback);
            if (type == nullptr) {
                throw std::logic_error(
                    "could not convert Python exception into a C++ exception - "
                    "exception is not set."
                );
            } else if (traceback == nullptr && value != nullptr) {
                traceback = PyException_GetTraceback(value);  // new ref
            }
            thread->curexc_type = nullptr;
            thread->curexc_value = nullptr;
            thread->curexc_traceback = nullptr;
        #endif

        // Re-throw the current exception as a registered bertrand exception type
        auto it = impl::rethrow_exception_map.find(type);
        try {
            if (it != impl::rethrow_exception_map.end()) {
                it->second(type, value, traceback, ++skip, thread);
            } else {
                throw Exception(type, value, traceback, ++skip, thread);
            }
        } catch (...) {
            Py_XDECREF(type);
            Py_XDECREF(value);
            Py_XDECREF(traceback);
            throw;
        }

        // This error is unreachable.  It is only here to ensure the compiler correctly
        // respects the [[noreturn]] attribute at the call site.
        throw std::logic_error(
            "Control reached end of [[noreturn]] bertrand::py::Exception::from_python()"
            "without catching an active exception"
        );
    }

    /* Retrieve an error from a pybind11 context and re-throw it as a C++ error with a
    matching type.  This is used to standardize all exceptions to the existing Python
    syntax, removing any special cases related to specific pybind11 error types.  Note
    that this is a void function that always throws. */
    [[noreturn]] BERTRAND_NOINLINE static void from_pybind11(
        size_t skip = 0,
        PyThreadState* thread = nullptr
    );

};


bool Exception::registered = impl::register_exception<Exception>(PyExc_Exception);


BERTRAND_EXCEPTION(ArithmeticError, Exception, PyExc_ArithmeticError)
    BERTRAND_EXCEPTION(FloatingPointError, ArithmeticError, PyExc_FloatingPointError)
    BERTRAND_EXCEPTION(OverflowError, ArithmeticError, PyExc_OverflowError)
    BERTRAND_EXCEPTION(ZeroDivisionError, ArithmeticError, PyExc_ZeroDivisionError)
BERTRAND_EXCEPTION(AssertionError, Exception, PyExc_AssertionError)
BERTRAND_EXCEPTION(AttributeError, Exception, PyExc_AttributeError)
BERTRAND_EXCEPTION(BufferError, Exception, PyExc_BufferError)
BERTRAND_EXCEPTION(EOFError, Exception, PyExc_EOFError)
BERTRAND_EXCEPTION(ImportError, Exception, PyExc_ImportError)
    BERTRAND_EXCEPTION(ModuleNotFoundError, ImportError, PyExc_ModuleNotFoundError)
BERTRAND_EXCEPTION(LookupError, Exception, PyExc_LookupError)
    BERTRAND_EXCEPTION(IndexError, LookupError, PyExc_IndexError)
    BERTRAND_EXCEPTION(KeyError, LookupError, PyExc_KeyError)
BERTRAND_EXCEPTION(MemoryError, Exception, PyExc_MemoryError)
BERTRAND_EXCEPTION(NameError, Exception, PyExc_NameError)
    BERTRAND_EXCEPTION(UnboundLocalError, NameError, PyExc_UnboundLocalError)
BERTRAND_EXCEPTION(OSError, Exception, PyExc_OSError)
    BERTRAND_EXCEPTION(BlockingIOError, OSError, PyExc_BlockingIOError)
    BERTRAND_EXCEPTION(ChildProcessError, OSError, PyExc_ChildProcessError)
    BERTRAND_EXCEPTION(ConnectionError, OSError, PyExc_ConnectionError)
        BERTRAND_EXCEPTION(BrokenPipeError, ConnectionError, PyExc_BrokenPipeError)
        BERTRAND_EXCEPTION(ConnectionAbortedError, ConnectionError, PyExc_ConnectionAbortedError)
        BERTRAND_EXCEPTION(ConnectionRefusedError, ConnectionError, PyExc_ConnectionRefusedError)
        BERTRAND_EXCEPTION(ConnectionResetError, ConnectionError, PyExc_ConnectionResetError)
    BERTRAND_EXCEPTION(FileExistsError, OSError, PyExc_FileExistsError)
    BERTRAND_EXCEPTION(FileNotFoundError, OSError, PyExc_FileNotFoundError)
    BERTRAND_EXCEPTION(InterruptedError, OSError, PyExc_InterruptedError)
    BERTRAND_EXCEPTION(IsADirectoryError, OSError, PyExc_IsADirectoryError)
    BERTRAND_EXCEPTION(NotADirectoryError, OSError, PyExc_NotADirectoryError)
    BERTRAND_EXCEPTION(PermissionError, OSError, PyExc_PermissionError)
    BERTRAND_EXCEPTION(ProcessLookupError, OSError, PyExc_ProcessLookupError)
    BERTRAND_EXCEPTION(TimeoutError, OSError, PyExc_TimeoutError)
BERTRAND_EXCEPTION(ReferenceError, Exception, PyExc_ReferenceError)
BERTRAND_EXCEPTION(RuntimeError, Exception, PyExc_RuntimeError)
    BERTRAND_EXCEPTION(NotImplementedError, RuntimeError, PyExc_NotImplementedError)
    BERTRAND_EXCEPTION(RecursionError, RuntimeError, PyExc_RecursionError)
BERTRAND_EXCEPTION(StopAsyncIteration, Exception, PyExc_StopAsyncIteration)
BERTRAND_EXCEPTION(StopIteration, Exception, PyExc_StopIteration)
BERTRAND_EXCEPTION(SyntaxError, Exception, PyExc_SyntaxError)
    BERTRAND_EXCEPTION(IndentationError, SyntaxError, PyExc_IndentationError)
        BERTRAND_EXCEPTION(TabError, IndentationError, PyExc_TabError)
BERTRAND_EXCEPTION(SystemError, Exception, PyExc_SystemError)
BERTRAND_EXCEPTION(TypeError, Exception, PyExc_TypeError)
    BERTRAND_EXCEPTION(CastError, TypeError, PyExc_TypeError)
    BERTRAND_EXCEPTION(ReferenceCastError, TypeError, PyExc_TypeError)
BERTRAND_EXCEPTION(ValueError, Exception, PyExc_ValueError)
    BERTRAND_EXCEPTION(UnicodeError, ValueError, PyExc_UnicodeError)
        BERTRAND_EXCEPTION(UnicodeDecodeError, UnicodeError, PyExc_UnicodeDecodeError)
        BERTRAND_EXCEPTION(UnicodeEncodeError, UnicodeError, PyExc_UnicodeEncodeError)
        BERTRAND_EXCEPTION(UnicodeTranslateError, UnicodeError, PyExc_UnicodeTranslateError)


[[noreturn]] BERTRAND_NOINLINE void Exception::from_pybind11(
    size_t skip,
    PyThreadState* thread
) {
    if (thread == nullptr) {
        thread = PyThreadState_Get();
    }
    try {
        throw;
    } catch (const pybind11::error_already_set& err) {
        PyObject* type = err.type().ptr();
        PyObject* value = err.value().ptr();
        PyObject* traceback = Py_XNewRef(err.trace().ptr());
        if (traceback == nullptr && value != nullptr) {
            traceback = PyException_GetTraceback(value);  // new ref
        }
        try {
            auto it = impl::rethrow_exception_map.find(type);
            if (it != impl::rethrow_exception_map.end()) {
                it->second(type, value, traceback, ++skip, thread);
            } else {
                throw Exception(type, value, traceback, ++skip, thread);
            }
        } catch (...) {
            Py_XDECREF(traceback);
            throw;
        }
    } catch (const pybind11::stop_iteration& err) {
        throw StopIteration(err.what(), ++skip, thread);
    } catch (const pybind11::index_error& err) {
        throw IndexError(err.what(), ++skip, thread);
    } catch (const pybind11::key_error& err) {
        throw KeyError(err.what(), ++skip, thread);
    } catch (const pybind11::value_error& err) {
        throw ValueError(err.what(), ++skip, thread);
    } catch (const pybind11::type_error& err) {
        throw TypeError(err.what(), ++skip, thread);
    } catch (const pybind11::buffer_error& err) {
        throw BufferError(err.what(), ++skip, thread);
    } catch (const pybind11::import_error& err) {
        throw ImportError(err.what(), ++skip, thread);
    } catch (const pybind11::attribute_error& err) {
        throw AttributeError(err.what(), ++skip, thread);
    }

    // This error is unreachable.  It is only here to ensure the compiler correctly
    // respects the [[noreturn]] attribute at the call site.
    throw std::logic_error(
        "Control reached end of [[noreturn]] bertrand::py::Exception::from_pybind11()"
        "without catching an active exception"
    );
}


}  // namespace py
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_COMMON_EXCEPTIONS_H
