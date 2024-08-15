#ifndef BERTRAND_PYTHON_COMMON_EXCEPT_H
#define BERTRAND_PYTHON_COMMON_EXCEPT_H

#include "declarations.h"
#include "pyerrors.h"


// TODO: UnicodeDecodeError requires extra arguments besides PyErr_SetString.  See
// PyUnicodeDecodeError_Create() for details.


namespace py {


namespace impl {

    /* A language-agnostic stack frame that is used when reporting mixed Python/C++
    error tracebacks. */
    struct StackFrame : BertrandTag {
    private:
        PyThreadState* thread;
        mutable PyFrameObject* py_frame = nullptr;
        mutable std::string string;

        /* Parse a function name and collapse `bertrand::StaticStr` objects as template
        arguments. */
        static std::string parse_function_name(const std::string& name) {
            /* NOTE: functions and classes that accept static strings as template
             * arguments are decomposed into numeric character arrays in the symbol
             * name, which need to be reconstructed here.  Here's an example:
             *      File "/home/eerkela/data/bertrand/bertrand/python/common/item.h",
             *      line 268, in bertrand::py::impl::Attr<bertrand::py::Object,
             *      bertrand::StaticStr<7ul>{char [8]{(char)95, (char)95, (char)103,
             *      (char)101, (char)116, (char)95, (char)95}}>::get_attr() const
             *
             * Our goal is to replace the `bertrand::StaticStr<7ul>{char [8]{...}}`
             * bit with the text it represents, in this case the string `"__get__"`.
             */
            size_t pos = name.find("bertrand::StaticStr<");
            if (pos == std::string::npos) {
                return name;
            }
            std::string result;
            size_t last = 0;
            while (pos != std::string::npos) {
                result += name.substr(last, pos - last) + '"';
                pos = name.find("]{", pos) + 2;
                size_t end = name.find("}}", pos);

                // extract the first number
                pos += 6;  // skip "(char)"
                while (pos < end) {
                    size_t next = std::min(end, name.find(',', pos));
                    result += static_cast<char>(std::stoi(
                        name.substr(pos, next - pos))
                    );
                    if (next == end) {
                        pos = end + 2;  // skip "}}"
                    } else {
                        pos = next + 8;  // skip ", (char)"
                    }
                }
                result += '"';
                last = pos;
                pos = name.find("bertrand::StaticStr<", pos);
            }
            return result + name.substr(last);
        }

    public:
        std::string filename;
        std::string funcname;
        int lineno = 0;
        bool is_inline = false;

        StackFrame(
            const std::string& filename,
            const std::string& funcname,
            int lineno,
            bool is_inline = false,
            PyThreadState* tstate = nullptr
        ) :
            thread(tstate == nullptr ? PyThreadState_Get() : tstate),
            filename(filename),
            funcname(parse_function_name(funcname)),
            lineno(lineno),
            is_inline(is_inline)
        {}

        StackFrame(
            const cpptrace::stacktrace_frame& frame,
            PyThreadState* tstate = nullptr
        ) :
            thread(tstate == nullptr ? PyThreadState_Get() : tstate),
            filename(frame.filename),
            funcname(parse_function_name(frame.symbol)),
            lineno(frame.line.value_or(0)),
            is_inline(frame.is_inline)
        {}

        StackFrame(PyFrameObject* frame = nullptr, PyThreadState* tstate = nullptr) :
            thread(tstate == nullptr ? PyThreadState_Get() : tstate),
            py_frame(reinterpret_cast<PyFrameObject*>(Py_XNewRef(frame)))
        {
            if (py_frame != nullptr) {
                PyCodeObject* code = PyFrame_GetCode(py_frame);
                if (code != nullptr) {
                    filename = PyUnicode_AsUTF8(code->co_filename);
                    funcname = PyUnicode_AsUTF8(code->co_name);
                    Py_DECREF(code);
                }
                lineno = PyFrame_GetLineNumber(frame);
            }
        }

        StackFrame(const StackFrame& other) :
            thread(other.thread),
            py_frame(reinterpret_cast<PyFrameObject*>(Py_XNewRef(other.py_frame))),
            string(other.string),
            filename(other.filename),
            funcname(other.funcname),
            lineno(other.lineno),
            is_inline(other.is_inline)
        {}

        StackFrame(StackFrame&& other) :
            thread(other.thread),
            py_frame(other.py_frame),
            string(std::move(other.string)),
            filename(std::move(other.filename)),
            funcname(std::move(other.funcname)),
            lineno(other.lineno),
            is_inline(other.is_inline)
        {
            other.thread = nullptr;
            other.py_frame = nullptr;
        }

        StackFrame& operator=(const StackFrame& other) {
            if (&other != this) {
                PyFrameObject* old_frame = py_frame;
                thread = other.thread;
                py_frame = reinterpret_cast<PyFrameObject*>(Py_XNewRef(other.py_frame));
                string = other.string;
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
                thread = other.thread;
                py_frame = other.py_frame;
                string = std::move(other.string);
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
                PyCodeObject* code;
                if (is_inline) {
                    code = PyCode_NewEmpty(
                        filename.c_str(),
                        ("[inline] " + funcname).c_str(),
                        lineno
                    );
                } else {
                    code = PyCode_NewEmpty(
                        filename.c_str(),
                        funcname.c_str(),
                        lineno
                    );
                }
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

        /* Convert this stack frame into a string representation, for use in C++
        exception tracebacks. */
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
    struct StackTrace : BertrandTag {
    private:
        mutable std::string string;
        mutable PyTracebackObject* py_traceback = nullptr;
        PyThreadState* thread;

        inline static const char* virtualenv = std::getenv("BERTRAND_HOME");

        /* Return true if a C++ stack frame originates from a blacklisted context. */
        static bool ignore(const cpptrace::stacktrace_frame& frame) {
            return (
                frame.symbol.starts_with("__") ||
                (virtualenv != nullptr && frame.filename.starts_with(virtualenv))
            );
        }

    public:
        // stack is stored in proper execution order
        std::deque<StackFrame> stack;  // [head] least recent -> most recent [tail]

        [[clang::noinline]] explicit StackTrace(
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

        [[nodiscard]] size_t size() const noexcept { return stack.size(); }
        [[nodiscard]] auto begin() const noexcept { return stack.begin(); }
        [[nodiscard]] auto end() const noexcept { return stack.end(); }
        [[nodiscard]] auto rbegin() const noexcept { return stack.rbegin(); }
        [[nodiscard]] auto rend() const noexcept { return stack.rend(); }

        /* Set an active Python error with this traceback. */
        void restore(PyObject* type, const char* value) const {
            PyErr_Clear();
            PyTracebackObject* tb = to_python();
            PyErr_SetString(type, value);
            if (tb != nullptr) {
                PyException_SetTraceback(
                    thread->current_exception,
                    reinterpret_cast<PyObject*>(tb)
                );
            }
        }

        /* Build an equivalent Python traceback object for this stack trace.  The
        result is cached and reused on subsequent calls.  The user does not need to
        decrement its reference count. */
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
                        Py_XNewRef((*it).to_python())
                    );
                    tb->tb_lasti = PyFrame_GetLasti(tb->tb_frame) * sizeof(_Py_CODEUNIT);
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
        [[nodiscard]] const std::string& to_string() const noexcept {
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

}


/* Base exception class.  Appends a C++ stack trace that will be propagated up to
Python for cross-language diagnostics. */
struct Exception : public std::exception, impl::BertrandTag {
protected:
    std::string what_msg;
    mutable std::string what_cache;

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

    #ifdef BERTRAND_NO_TRACEBACK

        explicit Exception(
            const std::string& message = "",
            size_t skip = 0,
            PyThreadState* thread = nullptr
        ) : what_msg(message)
        {}

        explicit Exception(
            const std::string& message,
            const cpptrace::stacktrace& trace,
            PyThreadState* thread = nullptr
        ) : what_msg(message)
        {}

        explicit Exception(
            PyObject* type,
            PyObject* value,
            PyObject* traceback,
            size_t skip = 0,
            PyThreadState* thread = nullptr
        ) : what_msg(value == nullptr ? std::string() : parse_value(value))
        {
            if (type == nullptr) {
                throw std::logic_error(
                    "could not convert Python exception into a C++ exception - "
                    "exception type is not set."
                );
            }
        }

        explicit Exception(
            PyObject* type,
            PyObject* value,
            PyObject* traceback,
            const cpptrace::stacktrace& trace,
            PyThreadState* thread = nullptr
        ) : what_msg(value == nullptr ? std::string() : parse_value(value))
        {
            if (type == nullptr) {
                throw std::logic_error(
                    "could not convert Python exception into a C++ exception - "
                    "exception type is not set."
                );
            }
        }

        Exception(const Exception& other) :
            std::exception(other), what_msg(other.what_msg),
            what_cache(other.what_cache)
        {}

        Exception& operator=(const Exception& other) {
            if (&other != this) {
                std::exception::operator=(other);
                what_msg = other.what_msg;
                what_cache = other.what_cache;
            }
            return *this;
        }

        /* Generate the message that will be printed if this error is propagated to a C++
        context without being explicitly caught.  If debug symbols are enabled, then this
        will include a Python-style traceback covering both the Python and C++ frames that
        were traversed to reach the error. */
        [[nodiscard]] const char* what() const noexcept override {
            if (what_cache.empty()) {
                what_cache = "Exception: " + message();
            }
            return what_cache.c_str();
        }

        /* Convert this exception into an equivalent Python error, so that it can be
        propagated to a Python context.  The resulting traceback reflects both the Python
        and C++ frames that were traversed to reach the error.  */
        virtual void set_pyerr() const {
            PyErr_SetString(PyExc_Exception, message().c_str());
        }

    #else
        impl::StackTrace traceback;

        [[clang::noinline]] explicit Exception(
            std::string message = "",
            size_t skip = 0,
            PyThreadState* thread = nullptr
        ) : what_msg((Interpreter::init(), message)),
            traceback(get_trace(skip), thread)
        {}

        [[clang::noinline]] explicit Exception(
            std::string message,
            const cpptrace::stacktrace& trace,
            PyThreadState* thread = nullptr
        ) : what_msg((Interpreter::init(), message)),
            traceback(trace, thread)
        {}

        [[clang::noinline]] explicit Exception(
            PyObject* type,
            PyObject* value,
            PyObject* traceback,
            size_t skip = 0,
            PyThreadState* thread = nullptr
        ) : what_msg(value == nullptr ? std::string() : parse_value(value)),
            traceback(
                reinterpret_cast<PyTracebackObject*>(traceback),
                get_trace(skip),
                thread
            )
        {
            if (type == nullptr) {
                throw std::logic_error(
                    "could not convert Python exception into a C++ exception - "
                    "exception type is not set."
                );
            }
        }

        [[clang::noinline]] explicit Exception(
            PyObject* type,
            PyObject* value,
            PyObject* traceback,
            const cpptrace::stacktrace& trace,
            PyThreadState* thread = nullptr
        ) : what_msg(value == nullptr ? std::string() : parse_value(value)),
            traceback(
                reinterpret_cast<PyTracebackObject*>(traceback),
                trace,
                thread
            )
        {
            if (type == nullptr) {
                throw std::logic_error(
                    "could not convert Python exception into a C++ exception - "
                    "exception type is not set."
                );
            }
        }

        Exception(const Exception& other) :
            std::exception(other), what_msg(other.what_msg),
            what_cache(other.what_cache), traceback(other.traceback)
        {}

        Exception& operator=(const Exception& other) {
            if (&other != this) {
                std::exception::operator=(other);
                what_msg = other.what_msg;
                what_cache = other.what_cache;
                traceback = other.traceback;
            }
            return *this;
        }

        /* Generate the message that will be printed if this error is propagated to a C++
        context without being explicitly caught.  If debug symbols are enabled, then this
        will include a Python-style traceback covering both the Python and C++ frames that
        were traversed to reach the error. */
        [[nodiscard]] const char* what() const noexcept override {
            if (what_cache.empty()) {
                what_cache = traceback.to_string() + "\nException: " + message();
            }
            return what_cache.c_str();
        }

        /* Convert this exception into an equivalent Python error, so that it can be
        propagated to a Python context.  The resulting traceback reflects both the Python
        and C++ frames that were traversed to reach the error.  */
        virtual void set_pyerr() const {
            traceback.restore(PyExc_Exception, message().c_str());
        }

    #endif

    /* Get the base error message without a traceback. */
    [[nodiscard]] const std::string& message() const noexcept {
        return what_msg;
    }

    /* Retrieve an error from a Python context and re-throw it as a C++ error with a
    matching type.  Note that this is a void function that always throws. */
    [[noreturn, clang::noinline]] static void from_python(
        size_t skip = 0,
        PyThreadState* thread = nullptr
    );  // defined after Module<"bertrand.python">, since it requires global state

    /* Convert an arbitrary C++ error into an equivalent Python exception, so that it
    can be propagate back to the Python interpreter. */
    static void to_python() {
        try {
            throw;
        } catch (const Exception& err) {
            err.set_pyerr();
        } catch (const std::exception& err) {
            PyErr_SetString(PyExc_Exception, err.what());
        } catch (...) {
            PyErr_SetString(PyExc_Exception, "unknown C++ exception");
        }
    }

};


/* Helper for generating new exception types that are compatible with Python.  This
automatically handles the preprocessor logic for the BERTRAND_NO_TRACEBACK flag,
disables the Exception::from_python() handler, and ensures that stack traces always
terminate at the exception constructor and no later. */
template <typename CRTP, std::derived_from<Exception> Base>
struct __exception__ : Base {

    #ifdef BERTRAND_NO_TRACEBACK

        const char* what() const noexcept override {
            if (Base::what_cache.empty()) {
                Base::what_cache =
                    impl::demangle(typeid(CRTP).name()) + ": " + Base::message();
            }
            return Base::what_cache.c_str();
        }

    #else

        [[clang::noinline]] explicit __exception__(
            const std::string& message = "",
            size_t skip = 0,
            PyThreadState* thread = nullptr
        ) : Base(message, Base::get_trace(skip), thread)
        {}

        [[clang::noinline]] explicit __exception__(
            const std::string& message,
            const cpptrace::stacktrace& trace,
            PyThreadState* thread = nullptr
        ) : Base(message, trace, thread)
        {}

        [[clang::noinline]] explicit __exception__(
            PyObject* type,
            PyObject* value,
            PyObject* traceback,
            size_t skip = 0,
            PyThreadState* thread = nullptr
        ) : Base(type, value, traceback, Base::get_trace(skip), thread)
        {}

        [[clang::noinline]] explicit __exception__(
            PyObject* type,
            PyObject* value,
            PyObject* traceback,
            const cpptrace::stacktrace& trace,
            PyThreadState* thread = nullptr
        ) : Base(type, value, traceback, trace, thread)
        {}

        const char* what() const noexcept override {
            if (Base::what_cache.empty()) {
                Base::what_cache =
                    Base::traceback.to_string() + "\n" +
                    impl::demangle(typeid(CRTP).name()) + ": " + Base::message();
            }
            return Base::what_cache.c_str();
        }

    #endif

    void set_pyerr() const override;

    static void from_python(
        size_t skip = 0,
        PyThreadState* thread = nullptr
    ) = delete;

};


/* CPython exception types:
 *      https://docs.python.org/3/c-api/exceptions.html#standard-exceptions
 *
 * Inheritance hierarchy:
 *      https://docs.python.org/3/library/exceptions.html#exception-hierarchy
 */


#ifdef BERTRAND_NO_TRACEBACK
    #define PYERR(TYPE) \
        void set_pyerr() const override { \
            PyErr_SetString(TYPE, message().c_str()); \
        }
#else
    #define PYERR(TYPE) \
        void set_pyerr() const override { \
            traceback.restore(TYPE, message().c_str()); \
        }
#endif


struct ArithmeticError : __exception__<ArithmeticError, Exception> {
    using __exception__::__exception__;
    PYERR(PyExc_ArithmeticError)
};


struct FloatingPointError : __exception__<FloatingPointError, ArithmeticError> {
    using __exception__::__exception__;
    PYERR(PyExc_FloatingPointError)
};


struct OverflowError : __exception__<OverflowError, ArithmeticError> {
    using __exception__::__exception__;
    PYERR(PyExc_OverflowError)
};


struct ZeroDivisionError : __exception__<ZeroDivisionError, ArithmeticError> {
    using __exception__::__exception__;
    PYERR(PyExc_ZeroDivisionError)
};


struct AssertionError : __exception__<AssertionError, Exception> {
    using __exception__::__exception__;
    PYERR(PyExc_AssertionError)
};


struct AttributeError : __exception__<AttributeError, Exception> {
    using __exception__::__exception__;
    PYERR(PyExc_AttributeError)
};


struct BufferError : __exception__<BufferError, Exception> {
    using __exception__::__exception__;
    PYERR(PyExc_BufferError)
};


struct EOFError : __exception__<EOFError, Exception> {
    using __exception__::__exception__;
    PYERR(PyExc_EOFError)
};


struct ImportError : __exception__<ImportError, Exception> {
    using __exception__::__exception__;
    PYERR(PyExc_ImportError)
};


struct ModuleNotFoundError : __exception__<ModuleNotFoundError, ImportError> {
    using __exception__::__exception__;
    PYERR(PyExc_ModuleNotFoundError)
};


struct LookupError : __exception__<LookupError, Exception> {
    using __exception__::__exception__;
    PYERR(PyExc_LookupError)
};


struct IndexError : __exception__<IndexError, LookupError> {
    using __exception__::__exception__;
    PYERR(PyExc_IndexError)
};


struct KeyError : __exception__<KeyError, LookupError> {
    using __exception__::__exception__;
    PYERR(PyExc_KeyError)
};


struct MemoryError : __exception__<MemoryError, Exception> {
    using __exception__::__exception__;
    PYERR(PyExc_MemoryError)
};


struct NameError : __exception__<NameError, Exception> {
    using __exception__::__exception__;
    PYERR(PyExc_NameError)
};


struct UnboundLocalError : __exception__<UnboundLocalError, NameError> {
    using __exception__::__exception__;
    PYERR(PyExc_UnboundLocalError)
};


struct OSError : __exception__<OSError, Exception> {
    using __exception__::__exception__;
    PYERR(PyExc_OSError)
};


struct BlockingIOError : __exception__<BlockingIOError, OSError> {
    using __exception__::__exception__;
    PYERR(PyExc_BlockingIOError)
};


struct ChildProcessError : __exception__<ChildProcessError, OSError> {
    using __exception__::__exception__;
    PYERR(PyExc_ChildProcessError)
};


struct ConnectionError : __exception__<ConnectionError, OSError> {
    using __exception__::__exception__;
    PYERR(PyExc_ConnectionError)
};


struct BrokenPipeError : __exception__<BrokenPipeError, ConnectionError> {
    using __exception__::__exception__;
    PYERR(PyExc_BrokenPipeError)
};


struct ConnectionAbortedError : __exception__<ConnectionAbortedError, ConnectionError> {
    using __exception__::__exception__;
    PYERR(PyExc_ConnectionAbortedError)
};


struct ConnectionRefusedError : __exception__<ConnectionRefusedError, ConnectionError> {
    using __exception__::__exception__;
    PYERR(PyExc_ConnectionRefusedError)
};


struct ConnectionResetError : __exception__<ConnectionResetError, ConnectionError> {
    using __exception__::__exception__;
    PYERR(PyExc_ConnectionResetError)
};


struct FileExistsError : __exception__<FileExistsError, OSError> {
    using __exception__::__exception__;
    PYERR(PyExc_FileExistsError)
};


struct FileNotFoundError : __exception__<FileNotFoundError, OSError> {
    using __exception__::__exception__;
    PYERR(PyExc_FileNotFoundError)
};


struct InterruptedError : __exception__<InterruptedError, OSError> {
    using __exception__::__exception__;
    PYERR(PyExc_InterruptedError)
};


struct IsADirectoryError : __exception__<IsADirectoryError, OSError> {
    using __exception__::__exception__;
    PYERR(PyExc_IsADirectoryError)
};


struct NotADirectoryError : __exception__<NotADirectoryError, OSError> {
    using __exception__::__exception__;
    PYERR(PyExc_NotADirectoryError)
};


struct PermissionError : __exception__<PermissionError, OSError> {
    using __exception__::__exception__;
    PYERR(PyExc_PermissionError)
};


struct ProcessLookupError : __exception__<ProcessLookupError, OSError> {
    using __exception__::__exception__;
    PYERR(PyExc_ProcessLookupError)
};


struct TimeoutError : __exception__<TimeoutError, OSError> {
    using __exception__::__exception__;
    PYERR(PyExc_TimeoutError)
};


struct ReferenceError : __exception__<ReferenceError, Exception> {
    using __exception__::__exception__;
    PYERR(PyExc_ReferenceError)
};


struct RuntimeError : __exception__<RuntimeError, Exception> {
    using __exception__::__exception__;
    PYERR(PyExc_RuntimeError)
};


struct NotImplementedError : __exception__<NotImplementedError, RuntimeError> {
    using __exception__::__exception__;
    PYERR(PyExc_NotImplementedError)
};


struct RecursionError : __exception__<RecursionError, RuntimeError> {
    using __exception__::__exception__;
    PYERR(PyExc_RecursionError)
};


struct StopAsyncIteration : __exception__<StopAsyncIteration, Exception> {
    using __exception__::__exception__;
    PYERR(PyExc_StopAsyncIteration)
};


struct StopIteration : __exception__<StopIteration, Exception> {
    using __exception__::__exception__;
    PYERR(PyExc_StopIteration)
};


struct SyntaxError : __exception__<SyntaxError, Exception> {
    using __exception__::__exception__;
    PYERR(PyExc_SyntaxError)
};


struct IndentationError : __exception__<IndentationError, SyntaxError> {
    using __exception__::__exception__;
    PYERR(PyExc_IndentationError)
};


struct TabError : __exception__<TabError, IndentationError> {
    using __exception__::__exception__;
    PYERR(PyExc_TabError)
};


struct SystemError : __exception__<SystemError, Exception> {
    using __exception__::__exception__;
    PYERR(PyExc_SystemError)
};


struct TypeError : __exception__<TypeError, Exception> {
    using __exception__::__exception__;
    PYERR(PyExc_TypeError)
};


struct ValueError : __exception__<ValueError, Exception> {
    using __exception__::__exception__;
    PYERR(PyExc_ValueError)
};


struct UnicodeError : __exception__<UnicodeError, ValueError> {
    using __exception__::__exception__;
    PYERR(PyExc_UnicodeError)
};


// TODO: UnicodeDecodeError needs a different set_pyerr() implementation, since
// it requires extra arguments besides PyErr_SetString.


struct UnicodeDecodeError : __exception__<UnicodeDecodeError, UnicodeError> {
    using __exception__::__exception__;
    PYERR(PyExc_UnicodeDecodeError)
};


struct UnicodeEncodeError : __exception__<UnicodeEncodeError, UnicodeError> {
    using __exception__::__exception__;
    PYERR(PyExc_UnicodeEncodeError)
};


struct UnicodeTranslateError : __exception__<UnicodeTranslateError, UnicodeError> {
    using __exception__::__exception__;
    PYERR(PyExc_UnicodeTranslateError)
};


#undef PYERR


}  // namespace py


#endif
