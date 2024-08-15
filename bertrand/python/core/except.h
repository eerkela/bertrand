#ifndef BERTRAND_PYTHON_COMMON_EXCEPT_H
#define BERTRAND_PYTHON_COMMON_EXCEPT_H

#include "declarations.h"


/* CPython exception types:
 *      https://docs.python.org/3/c-api/exceptions.html#standard-exceptions
 *
 * Inheritance hierarchy:
 *      https://docs.python.org/3/library/exceptions.html#exception-hierarchy
 */


// TODO: UnicodeDecodeError requires extra arguments besides PyErr_SetString.  See
// PyUnicodeDecodeError_Create() for details.


namespace py {


namespace impl {

    /* A language-agnostic stack frame that is used when reporting mixed Python/C++
    error tracebacks. */
    class StackFrame : public BertrandTag {
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
    class StackTrace : public BertrandTag {
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

    /* Functions of this type are called to raise Python errors as equivalent C++
    exceptions. */
    using ExceptionCallback = std::function<void(
        PyObject* /* exc_type */,
        PyObject* /* exc_value */,
        PyObject* /* ext_traceback */,
        size_t /* skip */,
        PyThreadState* /* curr_thread */
    )>;

    /* A registry that maps Python exception types to ExceptionCallbacks, allowing
    them to be be reflected in C++ try/catch semantics. */
    inline auto& exception_map() {
        static std::unordered_map<PyObject*, ExceptionCallback> map;
        return map;
    }

    /* Register a new exception type, pushing it to the exception map.  This is
    automatically called by the BERTRAND_EXCEPTION() macro, so that users should never
    need to register exceptions themselves. */
    template <typename cpp_type>
    bool register_exception(PyObject* py_type) {
        auto it = exception_map().find(py_type);
        if (it == exception_map().end()) {
            exception_map().emplace(py_type, [](
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
            });
        }
        return true;
    }

    // TODO: BERTRAND_EXCEPTION needs to be lifted up to bertrand/python.h so that it
    // can potentially be used in bindings.  Right now, it's only visible within the
    // module.
    // -> Maybe when generating Python->C++ bindings, I can just write out the full
    //    exception class without the need for this macro.
    // -> Type<Exception>::__python__ can potentially also be used to register C++
    //    exceptions as well, but that would involve turning all of these into proper
    //    Object subclasses, which is prohibitive.
    // -> Probably what I should do is offer a separate Exception::def CRTP helper as a
    //    protected member, which handles the logic to represent C++ errors in Python
    //    or vice versa.  It can have __throw__ and __catch__ methods that back
    //    from_python() and to_python() respectively.
    // -> Such an approach may also fix the UnicodeDecodeError issue above.

    #ifndef BERTRAND_NO_TRACEBACK

        #define BERTRAND_EXCEPTION(cls, base, pytype)                                   \
            static_assert(                                                              \
                std::derived_from<base, ::py::Exception>,                               \
                "exception base class must derive from py::Exception"                   \
            );                                                                          \
                                                                                        \
            class cls : public base {                                                   \
                inline static bool registered =                                         \
                    ::py::impl::register_exception<cls>(pytype);                        \
                                                                                        \
            public:                                                                     \
                using base::base;                                                       \
                                                                                        \
                [[clang::noinline]] explicit cls(                                       \
                    const std::string& message = "",                                    \
                    size_t skip = 0,                                                    \
                    PyThreadState* thread = nullptr                                     \
                ) : base(message, get_trace(skip), thread)                              \
                {}                                                                      \
                                                                                        \
                [[clang::noinline]] explicit cls(                                       \
                    const std::string& message,                                         \
                    const cpptrace::stacktrace& trace,                                  \
                    PyThreadState* thread = nullptr                                     \
                ) : base(message, trace, thread)                                        \
                {}                                                                      \
                                                                                        \
                [[clang::noinline]] explicit cls(                                       \
                    PyObject* type,                                                     \
                    PyObject* value,                                                    \
                    PyObject* traceback,                                                \
                    size_t skip = 0,                                                    \
                    PyThreadState* thread = nullptr                                     \
                ) : base(type, value, traceback, get_trace(skip), thread)               \
                {}                                                                      \
                                                                                        \
                [[clang::noinline]] explicit cls(                                       \
                    PyObject* type,                                                     \
                    PyObject* value,                                                    \
                    PyObject* traceback,                                                \
                    const cpptrace::stacktrace& trace,                                  \
                    PyThreadState* thread = nullptr                                     \
                ) : base(type, value, traceback, trace, thread)                         \
                {}                                                                      \
                                                                                        \
                virtual const char* what() const noexcept override {                    \
                    if (what_cache.empty()) {                                           \
                        what_cache = traceback.to_string() + "\n"#cls": " + message();  \
                    }                                                                   \
                    return what_cache.c_str();                                          \
                }                                                                       \
                                                                                        \
                virtual void set_pyerr() const override {                               \
                    traceback.restore(pytype, message().c_str());                       \
                }                                                                       \
                                                                                        \
                static void from_python(                                                \
                    size_t skip = 0,                                                    \
                    PyThreadState* thread = nullptr                                     \
                ) = delete;                                                             \
            };                                                                          \

    #else

        #define BERTRAND_EXCEPTION(cls, base, pytype)                                   \
            static_assert(                                                              \
                std::derived_from<base, ::py::Exception>,                               \
                "exception base class must derive from py::Exception"                   \
            );                                                                          \
                                                                                        \
            class cls : public base {                                                   \
                inline static bool registered =                                         \
                    ::py::impl::register_exception<cls>(pytype);                        \
                                                                                        \
            public:                                                                     \
                using base::base;                                                       \
                                                                                        \
                virtual const char* what() const noexcept override {                    \
                    if (what_cache.empty()) {                                           \
                        what_cache = #cls": " + message();                              \
                    }                                                                   \
                    return what_cache.c_str();                                          \
                }                                                                       \
                                                                                        \
                virtual void set_pyerr() const override {                               \
                    PyErr_SetString(pytype, message().c_str());                         \
                }                                                                       \
                                                                                        \
                static void from_python(                                                \
                    size_t skip = 0,                                                    \
                    PyThreadState* thread = nullptr                                     \
                ) = delete;                                                             \
            };                                                                          \

    #endif

}


/* Base exception class.  Appends a C++ stack trace that will be propagated up to
Python for cross-language diagnostics. */
class Exception : public std::exception, public impl::BertrandTag {
    inline static bool registered =
        impl::register_exception<Exception>(PyExc_Exception);

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

    // TODO: I should probably remove the skip and thread arguments, since I'm almost
    // certainly not going to use them.

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
            const std::string& message = "",
            size_t skip = 0,
            PyThreadState* thread = nullptr
        ) : what_msg((Interpreter::init(), message)),
            traceback(get_trace(skip), thread)
        {}

        [[clang::noinline]] explicit Exception(
            const std::string& message,
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
    ) {
        if (thread == nullptr) {
            thread = PyThreadState_Get();
        }

        // interacting with the Python error state is rather clumsy and was recently
        // changed in Python 3.12, so we need to handle both cases
        PyObject* value = thread->current_exception;
        if (value == nullptr) {
            throw std::logic_error(
                "could not convert Python exception into a C++ exception - "
                "exception is not set."
            );
        }
        PyObject* type = PyObject_Type(value);
        PyObject* traceback = PyException_GetTraceback(value);
        thread->current_exception = nullptr;

        try {
            // Re-throw the current exception as a registered bertrand exception type
            auto it = impl::exception_map().find(type);
            if (it != impl::exception_map().end()) {
                it->second(type, value, traceback, ++skip, thread);
            }

            throw Exception(type, value, traceback, ++skip, thread);
        } catch (...) {
            Py_XDECREF(type);
            Py_XDECREF(value);
            Py_XDECREF(traceback);
            throw;
        }
    }

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
BERTRAND_EXCEPTION(ValueError, Exception, PyExc_ValueError)
    BERTRAND_EXCEPTION(UnicodeError, ValueError, PyExc_UnicodeError)
        BERTRAND_EXCEPTION(UnicodeDecodeError, UnicodeError, PyExc_UnicodeDecodeError)
        BERTRAND_EXCEPTION(UnicodeEncodeError, UnicodeError, PyExc_UnicodeEncodeError)
        BERTRAND_EXCEPTION(UnicodeTranslateError, UnicodeError, PyExc_UnicodeTranslateError)


}  // namespace py


#endif
