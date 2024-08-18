#ifndef BERTRAND_PYTHON_CORE_EXCEPT_H
#define BERTRAND_PYTHON_CORE_EXCEPT_H

#include "declarations.h"
#include "object.h"


namespace py {


///////////////////////////
////    STACK FRAME    ////
///////////////////////////


namespace impl {

    std::string parse_function_name(const std::string& name) {
        /* NOTE: functions and classes that accept static strings as template
         * arguments are decomposed into numeric character arrays in the symbol name,
         * which need to be reconstructed here.  Here's an example:
         *
         *      // TODO: create a new example
         *
         *      File "/home/eerkela/data/bertrand/bertrand/python/core/object.h",
         *      line 268, in py::impl::Attr<bertrand::py::Object,
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

}


/// TODO: insert Type<Frame> and Interface<Type<Frame>>


struct Frame;


template <>
struct Interface<Frame> {
    [[nodiscard]] bool has_code() const;
    [[nodiscard]] std::string to_string() const;
    [[nodiscard]] Frame back() const;

    /// TODO: these are forward declarations
    [[nodiscard]] Code code() const;
    [[nodiscard]] int line_number() const;
    [[nodiscard]] Dict<Str, Object> builtins() const;
    [[nodiscard]] Dict<Str, Object> globals() const;
    [[nodiscard]] Dict<Str, Object> locals() const;
    [[nodiscard]] std::optional<Object> generator() const;
    [[nodiscard]] int last_instruction() const;
    [[nodiscard]] Object get(const Str& name) const;
};


/* A CPython interpreter frame, which can be introspected or arranged into coherent
cross-language tracebacks. */
struct Frame : Object, Interface<Frame> {

    Frame(Handle h, borrowed_t t) : Object(h, t) {}
    Frame(Handle h, stolen_t t) : Object(h, t) {}

    template <typename... Args> requires (implicit_ctor<Frame>::template enable<Args...>)
    Frame(Args&&... args) : Object(
        implicit_ctor<Frame>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Frame>::template enable<Args...>)
    explicit Frame(Args&&... args) : Object(
        explicit_ctor<Frame>{},
        std::forward<Args>(args)...
    ) {}

};


/* Default initializing a Frame object retrieves the currently-executing Python frame,
if one exists.  Note that this frame is guaranteed to have a valid Python bytecode
object, unlike the C++ frames of a Traceback object. */
template <>
struct __init__<Frame> : Returns<Frame> {
    static auto operator()();
};


/* Converting a `cpptrace::stacktrace_frame` into a Python frame object will synthesize
an interpreter frame with an empty bytecode object. */
template <>
struct __init__<Frame, cpptrace::stacktrace_frame>          : Returns<Frame> {
    static auto operator()(const cpptrace::stacktrace_frame& frame) {
        PyObject* globals = PyDict_New();
        if (globals == nullptr) {
            throw std::runtime_error(
                "could not convert stack frame into Python frame object - "
                "failed to create globals dictionary"
            );
        }

        std::string funcname = impl::parse_function_name(frame.symbol);
        unsigned int line = frame.line.value_or(0);
        if (frame.is_inline) {
            funcname = "[inline] " + funcname;
        }
        PyCodeObject* code = PyCode_NewEmpty(
            frame.filename.c_str(),
            funcname.c_str(),
            line
        );
        if (code == nullptr) {
            Py_DECREF(globals);
            throw std::runtime_error(
                "could not convert stack frame into Python frame object - "
                "failed to create code object"
            );
        }

        PyFrameObject* result = PyFrame_New(
            PyThreadState_Get(),
            code,
            globals,
            nullptr
        );
        Py_DECREF(globals);
        Py_DECREF(code);
        if (result == nullptr) {
            throw std::runtime_error(
                "could not convert stack frame into Python frame object - "
                "failed to initialize empty interpreter frame"
            );
        }
        result->f_lineno = line;
        return reinterpret_steal<Frame>(reinterpret_cast<PyObject*>(result));
    }
};


/* Providing an explicit integer will skip that number of frames from either the least
recent Python frame (if positive or zero) or the most recent (if negative).  Like the
default constructor, this always retrieves a frame with a valid Python bytecode object,
unlike the C++ frames of a Traceback object. */
template <std::convertible_to<int> T>
struct __explicit_init__<Frame, T>                          : Returns<Frame> {
    static Frame operator()(int skip);
};


/* Execute the bytecode object stored within a Python frame using its current context.
This is the main entry point for the Python interpreter, and causes the program to run
until it either terminates or encounters an error.  The return value is the result of
the last evaluated expression, which can be the return value of a function, the yield
value of a generator, etc. */
template <>
struct __call__<Frame> : Returns<Object> {
    static auto operator()(const Frame& frame);
};


[[nodiscard]] bool Interface<Frame>::has_code() const {
    return PyFrame_GetCode(
        reinterpret_cast<PyFrameObject*>(
            ptr(reinterpret_cast<const Object&>(*this))
        )
    ) != nullptr;
}



///////////////////////////
////    STACK TRACE    ////
///////////////////////////


namespace impl {

    inline const char* virtualenv = std::getenv("BERTRAND_HOME");

    inline bool ignore_frame(const cpptrace::stacktrace_frame& frame) {
        return (
            frame.symbol.starts_with("__") ||
            (virtualenv != nullptr && frame.filename.starts_with(virtualenv))
        );
    }

    inline void clear_traceback(PyTracebackObject* tb) {
        if (tb == nullptr) {
            return;
        } else {
            clear_traceback(tb->tb_next);
            Py_DECREF(tb);
        }
    }

}


/// TODO: insert Type<Traceback> and Interface<Type<Traceback>>


struct Traceback;


template <>
struct Interface<Traceback> {
    [[nodiscard]] std::string to_string() const;
};


/* A cross-language traceback that records an accurate call stack of a mixed Python/C++
application. */
struct Traceback : Object, Interface<Traceback> {

    Traceback(Handle h, borrowed_t t) : Object(h, t) {}
    Traceback(Handle h, stolen_t t) : Object(h, t) {}

    template <typename... Args> requires (implicit_ctor<Traceback>::template enable<Args...>)
    [[clang::noinline]] Traceback(Args&&... args) : Object(
        implicit_ctor<Traceback>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Traceback>::template enable<Args...>)
    [[clang::noinline]] explicit Traceback(Args&&... args) : Object(
        explicit_ctor<Traceback>{},
        std::forward<Args>(args)...
    ) {}

};


/* Converting a `cpptrace::stacktrace_frame` into a Python frame object will synthesize
an interpreter frame with an empty bytecode object. */
template <>
struct __init__<Traceback, cpptrace::stacktrace>            : Returns<Traceback> {
    static auto operator()(const cpptrace::stacktrace& trace) {
        // Traceback objects are stored in a singly-linked list, with the most recent
        // frame at the end of the list and the least frame at the beginning.  As a
        // result, we need to build them from the inside out, starting with C++ frames.
        PyTracebackObject* front = nullptr;

        try {
            // cpptrace stores frames in most recent -> least recent order, so inserting
            // into the singly-linked list will reverse the stack appropriately.
            for (auto&& frame : trace) {
                // stop the traceback if we encounter a C++ frame in which a nested
                // Python script was executed.
                if (frame.symbol.find("py::Code::operator()") != std::string::npos) {
                    break;

                // ignore frames that are not part of the user's code
                } else if (!impl::ignore_frame(frame)) {
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
                    tb->tb_next = front;
                    tb->tb_frame = reinterpret_cast<PyFrameObject*>(release(Frame(frame)));
                    tb->tb_lasti = PyFrame_GetLasti(tb->tb_frame) * sizeof(_Py_CODEUNIT);
                    tb->tb_lineno = PyFrame_GetLineNumber(tb->tb_frame);
                    PyObject_GC_Track(tb);
                    front = tb;
                }
            }

            // continue with the Python frames, again starting with the most recent
            PyFrameObject* frame = reinterpret_cast<PyFrameObject*>(
                Py_XNewRef(PyEval_GetFrame())
            );
            while (frame != nullptr) {
                PyTracebackObject* tb = PyObject_GC_New(
                    PyTracebackObject,
                    &PyTraceBack_Type
                );
                if (tb == nullptr) {
                    Py_DECREF(frame);
                    throw std::runtime_error(
                        "could not create Python traceback object - failed to allocate "
                        "PyTraceBackObject"
                    );
                }
                tb->tb_next = front;
                tb->tb_frame = frame;
                tb->tb_lasti = PyFrame_GetLasti(tb->tb_frame) * sizeof(_Py_CODEUNIT);
                tb->tb_lineno = PyFrame_GetLineNumber(tb->tb_frame);
                PyObject_GC_Track(tb);
                front = tb;
                frame = PyFrame_GetBack(frame);
            }

            return reinterpret_steal<Traceback>(reinterpret_cast<PyObject*>(front));

        } catch (...) {
            Py_XDECREF(front);
            throw;
        }
    }
};


/* Default initializing a Traceback object retrieves a trace to the current frame,
inserting C++ frames where necessary. */
template <>
struct __init__<Traceback>                                  : Returns<Traceback> {
    [[clang::noinline]] static auto operator()() {
        return Traceback(cpptrace::generate_trace(1));
    }
};


/* Providing an explicit integer will skip that number of frames from either the least
recent frame (if positive or zero) or the most recent (if negative).  Positive integers
will produce a traceback with at most the given length, and negative integers will
reduce the length by at most the given value. */
template <std::convertible_to<int> T>
struct __explicit_init__<Traceback, T>                      : Returns<Traceback> {
    static auto operator()(int skip) {
        // if skip is zero, then the result will be empty by definition
        if (skip == 0) {
            return reinterpret_steal<Traceback>(nullptr);
        }

        // compute the full traceback to account for mixed C++ and Python frames
        Traceback trace(cpptrace::generate_trace(1));
        PyTracebackObject* curr = reinterpret_cast<PyTracebackObject*>(
            ptr(trace)
        );

        // if skip is negative, we need to skip the most recent frames, which are
        // stored at the tail of the list.  Since we don't know the exact length of the
        // list, we can use a 2-pointer approach wherein the second pointer trails the
        // first by the given skip value.  When the first pointer reaches the end of
        // the list, the second pointer will be at the new terminal frame.
        if (skip < 0) {
            PyTracebackObject* offset = curr;
            for (int i = 0; i > skip; ++i) {
                // the traceback may be shorter than the skip value, in which case we
                // return an empty traceback
                if (curr == nullptr) {
                    return reinterpret_steal<Traceback>(nullptr);
                }
                curr = curr->tb_next;
            }

            while (curr != nullptr) {
                curr = curr->tb_next;
                offset = offset->tb_next;
            }

            // the offset pointer is now at the terminal frame, so we can safely remove
            // any subsequent frames.  Decrementing the reference count of the next
            // frame will garbage collect the remainder of the list.
            curr = offset->tb_next;
            offset->tb_next = nullptr;
            Py_DECREF(curr);
            return trace;
        }

        // if skip is positive, then we clear from the head, which is much simpler
        PyTracebackObject* prev = nullptr;
        for (int i = 0; i < skip; ++i) {
            // the traceback may be shorter than the skip value, in which case we return
            // the original traceback
            if (curr == nullptr) {
                return trace;
            }
            prev = curr;
            curr = curr->tb_next;
        }
        prev->tb_next = nullptr;
        Py_DECREF(curr);
        return trace;
    }
};


/* len(Traceback) yields the overall depth of the stack trace, including both C++ and
Python frames. */
template <>
struct __len__<Traceback>                                   : Returns<size_t> {
    static auto operator()(const Traceback& self) {
        PyTracebackObject* tb = reinterpret_cast<PyTracebackObject*>(ptr(self));
        size_t count = 0;
        while (tb != nullptr) {
            ++count;
            tb = tb->tb_next;
        }
        return count;
    }
};


/* Iterating over the frames yields them in least recent -> most recent order. */
template <>
struct __iter__<Traceback>                                  : Returns<Frame> {
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = Frame;
    using reference = Frame&;
    using pointer = Frame*;

    Traceback traceback;
    PyTracebackObject* curr;

    __iter__(const Traceback& self) : traceback(self), curr(nullptr) {}
    __iter__(Traceback&& self) : traceback(std::move(self)), curr(nullptr) {}

    __iter__(const Traceback& self, int) :
        traceback(self),
        curr(reinterpret_cast<PyTracebackObject*>(ptr(traceback)))
    {}

    __iter__(Traceback&& self, int) :
        traceback(std::move(self)),
        curr(reinterpret_cast<PyTracebackObject*>(ptr(traceback)))
    {}

    __iter__(const __iter__& other) :
        traceback(other.traceback),
        curr(other.curr)
    {}

    __iter__(__iter__&& other) :
        traceback(std::move(other.traceback)),
        curr(other.curr)
    {
        other.curr = nullptr;
    }

    __iter__& operator=(const __iter__& other) {
        if (&other != this) {
            traceback = other.traceback;
            curr = other.curr;
        }
        return *this;
    }

    __iter__& operator=(__iter__&& other) {
        if (&other != this) {
            traceback = std::move(other.traceback);
            curr = other.curr;
            other.curr = nullptr;
        }
        return *this;
    }

    [[nodiscard]] value_type operator*() const;

    __iter__& operator++() {
        if (curr != nullptr) {
            curr = curr->tb_next;
        }
        return *this;
    }

    __iter__ operator++(int) {
        __iter__ copy(*this);
        if (curr != nullptr) {
            curr = curr->tb_next;
        }
        return copy;
    }

    [[nodiscard]] bool operator==(const __iter__& other) const {
        return ptr(traceback) == ptr(other.traceback) && curr == other.curr;
    }

    [[nodiscard]] bool operator!=(const __iter__& other) const {
        return ptr(traceback) != ptr(other.traceback) || curr != other.curr;
    }
};


/* Reverse iterating over the frames yields them in most recent -> least recent order. */
template <>
struct __reversed__<Traceback>                              : Returns<Traceback> {
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = Frame;
    using reference = Frame&;
    using pointer = Frame*;

    Traceback traceback;
    std::vector<PyTracebackObject*> frames;
    Py_ssize_t index;

    __reversed__(const Traceback& self) : traceback(self), index(-1) {}
    __reversed__(Traceback&& self) : traceback(std::move(self)), index(-1) {}

    __reversed__(const Traceback& self, int) : traceback(self) {
        PyTracebackObject* curr = reinterpret_cast<PyTracebackObject*>(
            ptr(traceback)
        );
        while (curr != nullptr) {
            frames.push_back(curr);
            curr = curr->tb_next;
        }
        index = std::ssize(frames) - 1;
    }

    __reversed__(Traceback&& self, int) : traceback(std::move(self)) {
        PyTracebackObject* curr = reinterpret_cast<PyTracebackObject*>(
            ptr(traceback)
        );
        while (curr != nullptr) {
            frames.push_back(curr);
            curr = curr->tb_next;
        }
        index = std::ssize(frames) - 1;
    }

    __reversed__(const __reversed__& other) :
        traceback(other.traceback),
        frames(other.frames),
        index(other.index)
    {}

    __reversed__(__reversed__&& other) :
        traceback(std::move(other.traceback)),
        frames(std::move(other.frames)),
        index(other.index)
    {
        other.index = -1;
    }

    __reversed__& operator=(const __reversed__& other) {
        if (&other != this) {
            traceback = other.traceback;
            frames = other.frames;
            index = other.index;
        }
        return *this;
    }

    __reversed__& operator=(__reversed__&& other) {
        if (&other != this) {
            traceback = std::move(other.traceback);
            frames = std::move(other.frames);
            index = other.index;
            other.index = -1;
        }
        return *this;
    }

    [[nodiscard]] value_type operator*() const;

    __reversed__& operator++() {
        if (index >= 0) {
            --index;
        }
        return *this;
    }

    __reversed__ operator++(int) {
        __reversed__ copy(*this);
        if (index >= 0) {
            --index;
        }
        return copy;
    }

    [[nodiscard]] bool operator==(const __reversed__& other) const {
        return ptr(traceback) == ptr(other.traceback) && index == other.index;
    }

    [[nodiscard]] bool operator!=(const __reversed__& other) const {
        return ptr(traceback) != ptr(other.traceback) || index != other.index;
    }
};


[[nodiscard]] std::string Interface<Traceback>::to_string() const {
    std::string out = "Traceback (most recent call last):";
    PyTracebackObject* tb = reinterpret_cast<PyTracebackObject*>(
        ptr(reinterpret_cast<const Object&>(*this))
    );
    while (tb != nullptr) {
        out += "\n  ";
        out += reinterpret_borrow<Frame>(
            reinterpret_cast<PyObject*>(tb->tb_frame)
        ).to_string();
        tb = tb->tb_next;
    }
    return out;
}


/////////////////////////
////    EXCEPTION    ////
/////////////////////////


/// TODO: Exception::from_python() will need to append C++ frames to the head of the
/// traceback.


/// TODO: OSError also accepts additional arguments, along with Unicode errors.


struct Exception;


template <>
struct Interface<Exception> {
    [[noreturn, clang::noinline]] static void from_python();  // defined in __init__.h
    static void to_python();
};


struct Exception : std::exception, Object, Interface<Exception> {
protected:
    mutable std::string m_message;
    mutable std::string m_what;

public:

    Exception(Handle h, borrowed_t t) : Object(h, t) {}
    Exception(Handle h, stolen_t t) : Object(h, t) {}

    template <typename... Args> requires (implicit_ctor<Exception>::template enable<Args...>)
    [[clang::noinline]] Exception(Args&&... args) : Object(
        implicit_ctor<Exception>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Exception>::template enable<Args...>)
    [[clang::noinline]] explicit Exception(Args&&... args) : Object(
        explicit_ctor<Exception>{},
        std::forward<Args>(args)...
    ) {}

    /* Returns the message that was supplied to construct this exception. */
    const char* message() const noexcept {
        PyObject* args = PyException_GetArgs(ptr(*this));
        if (args == nullptr) {
            PyErr_Clear();
            return "";
        } else if (PyTuple_GET_SIZE(args) == 0) {
            Py_DECREF(args);
            return "";
        }
        PyObject* msg = PyTuple_GET_ITEM(args, 0);
        Py_DECREF(args);
        if (msg == nullptr) {
            return "";
        }
        const char* result = PyUnicode_AsUTF8(msg);
        if (result == nullptr) {
            PyErr_Clear();
            return "";
        }
        return result;
    }

    /* Returns a Python-style traceback and error as a C++ string, which will be
    displayed in case of an uncaught error. */
    const char* what() const noexcept override {
        if (m_what.empty()) {
            Traceback tb = reinterpret_steal<Traceback>(
                PyException_GetTraceback(ptr(*this))
            );
            if (ptr(tb) != nullptr) {
                m_what += tb.to_string() + "\n";
            }
            m_what += Py_TYPE(ptr(*this))->tp_name;
            m_what += ": ";
            m_what += message();
        }
        return m_what.c_str();
    }

    /* Clear the error's message() and what() caches, forcing them to be recomputed
    the next time they are requested. */
    void flush() const noexcept {
        m_message.clear();
        m_what.clear();
    }

};


// TODO: standard exceptions need to pipe the type into this constructor.


template <std::derived_from<Exception> Exc, typename Msg>
    requires (impl::string_literal<Msg> || std::convertible_to<Msg, std::string>)
struct __explicit_init__<Exc, Msg>                          : Returns<Exc> {
    [[clang::noinline]] static auto operator()(const std::string& msg) {
        PyObject* result = PyObject_CallFunction(
            ptr(Type<Exc>()),
            "s",
            msg.c_str()
        );
        if (result == nullptr) {
            Exception::from_python();
        }

        // by default, the exception will have an empty traceback, so we need to
        // populate it with C++ frames if directed.
        #ifndef BERTRAND_NO_TRACEBACK
            PyTracebackObject* front = nullptr;
            try {
                // cpptrace stores frames in most recent -> least recent order, so
                // inserting into the singly-linked list will reverse the stack
                // appropriately.
                for (auto&& frame : cpptrace::generate_trace(1)) {
                    // stop the traceback if we encounter a C++ frame in which a nested
                    // Python script was executed.
                    if (frame.symbol.find("py::Code::operator()") != std::string::npos) {
                        break;

                    // ignore frames that are not part of the user's code
                    } else if (!impl::ignore_frame(frame)) {
                        PyTracebackObject* tb = PyObject_GC_New(
                            PyTracebackObject,
                            &PyTraceBack_Type
                        );
                        if (tb == nullptr) {
                            throw std::runtime_error(
                                "could not create Python traceback object - failed to "
                                "allocate PyTraceBackObject"
                            );
                        }
                        tb->tb_next = front;
                        tb->tb_frame = reinterpret_cast<PyFrameObject*>(
                            release(Frame(frame))
                        );
                        tb->tb_lasti =
                            PyFrame_GetLasti(tb->tb_frame) * sizeof(_Py_CODEUNIT);
                        tb->tb_lineno = PyFrame_GetLineNumber(tb->tb_frame);
                        PyObject_GC_Track(tb);
                        front = tb;
                    }
                }
                if (front != nullptr) {
                    PyException_SetTraceback(result, reinterpret_cast<PyObject*>(front));
                    Py_DECREF(front);
                }
            } catch (...) {
                Py_DECREF(result);
                Py_XDECREF(front);
                throw;
            }
        #endif

        return reinterpret_steal<Exc>(result);
    }
};


template <std::derived_from<Exception> Exc>
struct __init__<Exc> : Returns<Exc> {
    static auto operator()() {
        return Exc("");
    }
};


void Interface<Exception>::to_python() {
    try {
        throw;
    } catch (const Exception& err) {
        PyThreadState_Get()->current_exception = release(err);
    } catch (const std::exception& err) {
        PyErr_SetString(PyExc_Exception, err.what());
    } catch (...) {
        PyErr_SetString(PyExc_Exception, "unknown C++ exception");
    }
}


///////////////////////////////////
////    STANDARD EXCEPTIONS    ////
///////////////////////////////////


/* CPython exception types:
 *      https://docs.python.org/3/c-api/exceptions.html#standard-exceptions
 *
 * Inheritance hierarchy:
 *      https://docs.python.org/3/library/exceptions.html#exception-hierarchy
 */


#define BUILTIN_EXCEPTION(CLS, BASE)                                                    \
    struct CLS;                                                                         \
                                                                                        \
    template <>                                                                         \
    struct Interface<CLS> : Interface<BASE> {};                                         \
                                                                                        \
    struct CLS : Exception, Interface<CLS> {                                            \
        CLS(Handle h, borrowed_t t) : Exception(h, t) {}                                \
        CLS(Handle h, stolen_t t) : Exception(h, t) {}                                  \
                                                                                        \
        template <typename... Args> requires (implicit_ctor<CLS>::template enable<Args...>) \
        [[clang::noinline]] CLS(Args&&... args) : Exception(                            \
            implicit_ctor<CLS>{},                                                       \
            std::forward<Args>(args)...                                                 \
        ) {}                                                                            \
                                                                                        \
        template <typename... Args> requires (explicit_ctor<CLS>::template enable<Args...>) \
        [[clang::noinline]] explicit CLS(Args&&... args) : Exception(                   \
            explicit_ctor<CLS>{},                                                       \
            std::forward<Args>(args)...                                                 \
        ) {}                                                                            \
    };


BUILTIN_EXCEPTION(ArithmeticError, Exception)
    BUILTIN_EXCEPTION(FloatingPointError, ArithmeticError)
    BUILTIN_EXCEPTION(OverflowError, ArithmeticError)
    BUILTIN_EXCEPTION(ZeroDivisionError, ArithmeticError)
BUILTIN_EXCEPTION(AssertionError, Exception)
BUILTIN_EXCEPTION(AttributeError, Exception)
BUILTIN_EXCEPTION(BufferError, Exception)
BUILTIN_EXCEPTION(EOFError, Exception)
BUILTIN_EXCEPTION(ImportError, Exception)
    BUILTIN_EXCEPTION(ModuleNotFoundError, ImportError)
BUILTIN_EXCEPTION(LookupError, Exception)
    BUILTIN_EXCEPTION(IndexError, LookupError)
    BUILTIN_EXCEPTION(KeyError, LookupError)
BUILTIN_EXCEPTION(MemoryError, Exception)
BUILTIN_EXCEPTION(NameError, Exception)
    BUILTIN_EXCEPTION(UnboundLocalError, NameError)
BUILTIN_EXCEPTION(OSError, Exception)
    BUILTIN_EXCEPTION(BlockingIOError, OSError)
    BUILTIN_EXCEPTION(ChildProcessError, OSError)
    BUILTIN_EXCEPTION(ConnectionError, OSError)
        BUILTIN_EXCEPTION(BrokenPipeError, ConnectionError)
        BUILTIN_EXCEPTION(ConnectionAbortedError, ConnectionError)
        BUILTIN_EXCEPTION(ConnectionRefusedError, ConnectionError)
        BUILTIN_EXCEPTION(ConnectionResetError, ConnectionError)
    BUILTIN_EXCEPTION(FileExistsError, OSError)
    BUILTIN_EXCEPTION(FileNotFoundError, OSError)
    BUILTIN_EXCEPTION(InterruptedError, OSError)
    BUILTIN_EXCEPTION(IsADirectoryError, OSError)
    BUILTIN_EXCEPTION(NotADirectoryError, OSError)
    BUILTIN_EXCEPTION(PermissionError, OSError)
    BUILTIN_EXCEPTION(ProcessLookupError, OSError)
    BUILTIN_EXCEPTION(TimeoutError, OSError)
BUILTIN_EXCEPTION(ReferenceError, Exception)
BUILTIN_EXCEPTION(RuntimeError, Exception)
    BUILTIN_EXCEPTION(NotImplementedError, RuntimeError)
    BUILTIN_EXCEPTION(RecursionError, RuntimeError)
BUILTIN_EXCEPTION(StopAsyncIteration, Exception)
BUILTIN_EXCEPTION(StopIteration, Exception)
BUILTIN_EXCEPTION(SyntaxError, Exception)
    BUILTIN_EXCEPTION(IndentationError, SyntaxError)
        BUILTIN_EXCEPTION(TabError, IndentationError)
BUILTIN_EXCEPTION(SystemError, Exception)
BUILTIN_EXCEPTION(TypeError, Exception)
BUILTIN_EXCEPTION(ValueError, Exception)
    BUILTIN_EXCEPTION(UnicodeError, ValueError)
        // BUILTIN_EXCEPTION(UnicodeDecodeError, UnicodeError)
        // BUILTIN_EXCEPTION(UnicodeEncodeError, UnicodeError)
        // BUILTIN_EXCEPTION(UnicodeTranslateError, UnicodeError)


// struct UnicodeDecodeError : Exception, Interface<UnicodeDecodeError> {
//     using Base = __exception__<UnicodeDecodeError, UnicodeError>;

// protected:

//     static std::string generate_message(
//         const std::string& encoding,
//         const std::string& object,
//         Py_ssize_t start,
//         Py_ssize_t end,
//         const std::string& reason
//     ) {
//         return (
//             "'" + encoding + "' codec can't decode bytes in position " +
//             std::to_string(start) + "-" + std::to_string(end - 1) + ": " +
//             reason
//         );
//     }

// public:
//     std::string encoding;
//     std::string object;
//     Py_ssize_t start;
//     Py_ssize_t end;
//     std::string reason;

//     [[clang::noinline]] explicit UnicodeDecodeError(
//         const std::string& encoding,
//         const std::string& object,
//         Py_ssize_t start,
//         Py_ssize_t end,
//         const std::string& reason,
//         size_t skip = 0
//     ) : UnicodeDecodeError(
//             encoding,
//             object,
//             start,
//             end,
//             reason,
//             cpptrace::generate_trace(skip + 2)
//         )
//     {}

//     [[clang::noinline]] explicit UnicodeDecodeError(
//         const std::string& encoding,
//         const std::string& object,
//         Py_ssize_t start,
//         Py_ssize_t end,
//         const std::string& reason,
//         const cpptrace::stacktrace& trace
//     ) : Base(generate_message(encoding, object, start, end, reason), trace),
//         encoding(encoding),
//         object(object),
//         start(start),
//         end(end),
//         reason(reason)
//     {}

//     [[clang::noinline]] explicit UnicodeDecodeError(
//         PyObject* type,
//         PyObject* value,
//         PyObject* traceback,
//         size_t skip = 0
//     ) : UnicodeDecodeError(
//         type,
//         value,
//         traceback,
//         cpptrace::generate_trace(skip + 2)
//     ) {}

//     [[clang::noinline]] explicit UnicodeDecodeError(
//         PyObject* type,
//         PyObject* value,
//         PyObject* traceback,
//         const cpptrace::stacktrace& trace
//     ) : Base(type, value, traceback, trace),
//         encoding([value] {
//             PyObject* encoding = PyUnicodeDecodeError_GetEncoding(value);
//             if (encoding == nullptr) {
//                 throw std::runtime_error(
//                     "could not extract encoding from UnicodeDecodeError"
//                 );
//             }
//             Py_ssize_t size;
//             const char* data = PyUnicode_AsUTF8AndSize(encoding, &size);
//             if (data == nullptr) {
//                 Py_DECREF(encoding);
//                 throw std::runtime_error(
//                     "could not extract encoding from UnicodeDecodeError"
//                 );
//             }
//             std::string result(data, size);
//             Py_DECREF(encoding);
//             return result;
//         }()),
//         object([value] {
//             PyObject* object = PyUnicodeDecodeError_GetObject(value);
//             if (object == nullptr) {
//                 throw std::runtime_error(
//                     "could not extract object from UnicodeDecodeError"
//                 );
//             }
//             Py_ssize_t size;
//             const char* data = PyUnicode_AsUTF8AndSize(object, &size);
//             if (data == nullptr) {
//                 Py_DECREF(object);
//                 throw std::runtime_error(
//                     "could not extract object from UnicodeDecodeError"
//                 );
//             }
//             std::string result(data, size);
//             Py_DECREF(object);
//             return result;
//         }()),
//         start([value] {
//             Py_ssize_t start;
//             if (PyUnicodeDecodeError_GetStart(value, &start)) {
//                 throw std::runtime_error(
//                     "could not extract start from UnicodeDecodeError"
//                 );
//             }
//             return start;
//         }()),
//         end([value] {
//             Py_ssize_t end;
//             if (PyUnicodeDecodeError_GetEnd(value, &end)) {
//                 throw std::runtime_error(
//                     "could not extract end from UnicodeDecodeError"
//                 );
//             }
//             return end;
//         }()),
//         reason([value] {
//             PyObject* reason = PyUnicodeDecodeError_GetReason(value);
//             if (reason == nullptr) {
//                 throw std::runtime_error(
//                     "could not extract reason from UnicodeDecodeError"
//                 );
//             }
//             Py_ssize_t size;
//             const char* data = PyUnicode_AsUTF8AndSize(reason, &size);
//             if (data == nullptr) {
//                 Py_DECREF(reason);
//                 throw std::runtime_error(
//                     "could not extract reason from UnicodeDecodeError"
//                 );
//             }
//             std::string result(data, size);
//             Py_DECREF(reason);
//             return result;
//         }())
//     {}

//     const char* what() const noexcept override {
//         if (what_cache.empty()) {
//             #ifndef BERTRAND_NO_TRACEBACK
//                 what_cache += traceback.to_string() + "\n";
//             #endif
//             what_cache += "UnicodeDecodeError: " + message();
//         }
//         return what_cache.c_str();
//     }

//     void set_pyerr() const override {
//         PyErr_Clear();
//         PyObject* exc = PyUnicodeDecodeError_Create(
//             encoding.c_str(),
//             object.c_str(),
//             object.size(),
//             start,
//             end,
//             reason.c_str()
//         );
//         if (exc == nullptr) {
//             PyErr_SetString(PyExc_UnicodeError, message().c_str());
//         } else {
//             PyTracebackObject* tb = traceback.to_python();
//             if (tb != nullptr) {
//                 PyException_SetTraceback(exc, reinterpret_cast<PyObject*>(tb));
//             }
//             PyThreadState* thread = PyThreadState_Get();
//             thread->current_exception = exc;
//         }
//     }

// };


// struct UnicodeEncodeError : __exception__<UnicodeEncodeError, UnicodeError> {
//     using Base = __exception__<UnicodeEncodeError, UnicodeError>;

// protected:

//     static std::string generate_message(
//         const std::string& encoding,
//         const std::string& object,
//         Py_ssize_t start,
//         Py_ssize_t end,
//         const std::string& reason
//     ) {
//         return (
//             "'" + encoding + "' codec can't encode characters in position " +
//             std::to_string(start) + "-" + std::to_string(end - 1) + ": " +
//             reason
//         );
//     }

// public:
//     std::string encoding;
//     std::string object;
//     Py_ssize_t start = 0;
//     Py_ssize_t end = 0;
//     std::string reason;

//     [[clang::noinline]] explicit UnicodeEncodeError(
//         const std::string& encoding,
//         const std::string& object,
//         Py_ssize_t start,
//         Py_ssize_t end,
//         const std::string& reason,
//         size_t skip = 0
//     ) : UnicodeEncodeError(
//             encoding,
//             object,
//             start,
//             end,
//             reason,
//             cpptrace::generate_trace(skip + 2)
//         )
//     {}

//     [[clang::noinline]] explicit UnicodeEncodeError(
//         const std::string& encoding,
//         const std::string& object,
//         Py_ssize_t start,
//         Py_ssize_t end,
//         const std::string& reason,
//         const cpptrace::stacktrace& trace
//     ) : Base(generate_message(encoding, object, start, end, reason), trace),
//         encoding(encoding),
//         object(object),
//         start(start),
//         end(end),
//         reason(reason)
//     {}

//     [[clang::noinline]] explicit UnicodeEncodeError(
//         PyObject* type,
//         PyObject* value,
//         PyObject* traceback,
//         size_t skip = 0
//     ) : UnicodeEncodeError(
//         type,
//         value,
//         traceback,
//         cpptrace::generate_trace(skip + 2)
//     ) {}

//     [[clang::noinline]] explicit UnicodeEncodeError(
//         PyObject* type,
//         PyObject* value,
//         PyObject* traceback,
//         const cpptrace::stacktrace& trace
//     ) : Base(type, value, traceback, trace),
//         encoding([value] {
//             PyObject* encoding = PyUnicodeEncodeError_GetEncoding(value);
//             if (encoding == nullptr) {
//                 throw std::runtime_error(
//                     "could not extract encoding from UnicodeEncodeError"
//                 );
//             }
//             Py_ssize_t size;
//             const char* data = PyUnicode_AsUTF8AndSize(encoding, &size);
//             if (data == nullptr) {
//                 Py_DECREF(encoding);
//                 throw std::runtime_error(
//                     "could not extract encoding from UnicodeEncodeError"
//                 );
//             }
//             std::string result(data, size);
//             Py_DECREF(encoding);
//             return result;
//         }()),
//         object([value] {
//             PyObject* object = PyUnicodeEncodeError_GetObject(value);
//             if (object == nullptr) {
//                 throw std::runtime_error(
//                     "could not extract object from UnicodeEncodeError"
//                 );
//             }
//             Py_ssize_t size;
//             const char* data = PyUnicode_AsUTF8AndSize(object, &size);
//             if (data == nullptr) {
//                 Py_DECREF(object);
//                 throw std::runtime_error(
//                     "could not extract object from UnicodeEncodeError"
//                 );
//             }
//             std::string result(data, size);
//             Py_DECREF(object);
//             return result;
//         }()),
//         start([value] {
//             Py_ssize_t start;
//             if (PyUnicodeEncodeError_GetStart(value, &start)) {
//                 throw std::runtime_error(
//                     "could not extract start from UnicodeEncodeError"
//                 );
//             }
//             return start;
//         }()),
//         end([value] {
//             Py_ssize_t end;
//             if (PyUnicodeEncodeError_GetEnd(value, &end)) {
//                 throw std::runtime_error(
//                     "could not extract end from UnicodeEncodeError"
//                 );
//             }
//             return end;
//         }()),
//         reason([value] {
//             PyObject* reason = PyUnicodeEncodeError_GetReason(value);
//             if (reason == nullptr) {
//                 throw std::runtime_error(
//                     "could not extract reason from UnicodeEncodeError"
//                 );
//             }
//             Py_ssize_t size;
//             const char* data = PyUnicode_AsUTF8AndSize(reason, &size);
//             if (data == nullptr) {
//                 Py_DECREF(reason);
//                 throw std::runtime_error(
//                     "could not extract reason from UnicodeEncodeError"
//                 );
//             }
//             std::string result(data, size);
//             Py_DECREF(reason);
//             return result;
//         }())
//     {}

//     const char* what() const noexcept override {
//         if (what_cache.empty()) {
//             #ifndef BERTRAND_NO_TRACEBACK
//                 what_cache += traceback.to_string() + "\n";
//             #endif
//             what_cache += "UnicodeEncodeError: " + message();
//         }
//         return what_cache.c_str();
//     }

//     void set_pyerr() const override {
//         PyErr_Clear();
//         PyObject* exc = PyObject_CallFunction(
//             PyExc_UnicodeEncodeError,
//             "ssnns",
//             encoding.c_str(),
//             object.c_str(),
//             start,
//             end,
//             reason.c_str()
//         );
//         if (exc == nullptr) {
//             PyErr_SetString(PyExc_UnicodeError, message().c_str());
//         } else {
//             PyTracebackObject* tb = traceback.to_python();
//             if (tb != nullptr) {
//                 PyException_SetTraceback(exc, reinterpret_cast<PyObject*>(tb));
//             }
//             PyThreadState* thread = PyThreadState_Get();
//             thread->current_exception = exc;
//         }
//     }

// };


// struct UnicodeTranslateError : __exception__<UnicodeTranslateError, UnicodeError> {
//     using Base = __exception__<UnicodeTranslateError, UnicodeError>;

// protected:

//     static std::string generate_message(
//         const std::string& object,
//         Py_ssize_t start,
//         Py_ssize_t end,
//         const std::string& reason
//     ) {
//         return (
//             "can't translate characters in position " + std::to_string(start) +
//             "-" + std::to_string(end - 1) + ": " + reason
//         );
//     }

// public:
//     std::string object;
//     Py_ssize_t start = 0;
//     Py_ssize_t end = 0;
//     std::string reason;

//     [[clang::noinline]] explicit UnicodeTranslateError(
//         const std::string& object,
//         Py_ssize_t start,
//         Py_ssize_t end,
//         const std::string& reason,
//         size_t skip = 0
//     ) : UnicodeTranslateError(
//             object,
//             start,
//             end,
//             reason,
//             cpptrace::generate_trace(skip + 2)
//         )
//     {}

//     [[clang::noinline]] explicit UnicodeTranslateError(
//         const std::string& object,
//         Py_ssize_t start,
//         Py_ssize_t end,
//         const std::string& reason,
//         const cpptrace::stacktrace& trace
//     ) : Base(generate_message(object, start, end, reason), trace),
//         object(object),
//         start(start),
//         end(end),
//         reason(reason)
//     {}

//     [[clang::noinline]] explicit UnicodeTranslateError(
//         PyObject* type,
//         PyObject* value,
//         PyObject* traceback,
//         size_t skip = 0
//     ) : UnicodeTranslateError(
//         type,
//         value,
//         traceback,
//         cpptrace::generate_trace(skip + 2)
//     ) {}

//     [[clang::noinline]] explicit UnicodeTranslateError(
//         PyObject* type,
//         PyObject* value,
//         PyObject* traceback,
//         const cpptrace::stacktrace& trace
//     ) : Base(type, value, traceback, trace),
//         object([value] {
//             PyObject* object = PyUnicodeTranslateError_GetObject(value);
//             if (object == nullptr) {
//                 throw std::runtime_error(
//                     "could not extract object from UnicodeTranslateError"
//                 );
//             }
//             Py_ssize_t size;
//             const char* data = PyUnicode_AsUTF8AndSize(object, &size);
//             if (data == nullptr) {
//                 Py_DECREF(object);
//                 throw std::runtime_error(
//                     "could not extract object from UnicodeTranslateError"
//                 );
//             }
//             std::string result(data, size);
//             Py_DECREF(object);
//             return result;
//         }()),
//         start([value] {
//             Py_ssize_t start;
//             if (PyUnicodeTranslateError_GetStart(value, &start)) {
//                 throw std::runtime_error(
//                     "could not extract start from UnicodeTranslateError"
//                 );
//             }
//             return start;
//         }()),
//         end([value] {
//             Py_ssize_t end;
//             if (PyUnicodeTranslateError_GetEnd(value, &end)) {
//                 throw std::runtime_error(
//                     "could not extract end from UnicodeTranslateError"
//                 );
//             }
//             return end;
//         }()),
//         reason([value] {
//             PyObject* reason = PyUnicodeTranslateError_GetReason(value);
//             if (reason == nullptr) {
//                 throw std::runtime_error(
//                     "could not extract reason from UnicodeTranslateError"
//                 );
//             }
//             Py_ssize_t size;
//             const char* data = PyUnicode_AsUTF8AndSize(reason, &size);
//             if (data == nullptr) {
//                 Py_DECREF(reason);
//                 throw std::runtime_error(
//                     "could not extract reason from UnicodeTranslateError"
//                 );
//             }
//             std::string result(data, size);
//             Py_DECREF(reason);
//             return result;
//         }())
//     {}

//     const char* what() const noexcept override {
//         if (what_cache.empty()) {
//             #ifndef BERTRAND_NO_TRACEBACK
//                 what_cache += traceback.to_string() + "\n";
//             #endif
//             what_cache += "UnicodeTranslateError: " + message();
//         }
//         return what_cache.c_str();
//     }

//     void set_pyerr() const override {
//         PyErr_Clear();
//         PyObject* exc = PyObject_CallFunction(
//             PyExc_UnicodeTranslateError,
//             "snns",
//             object.c_str(),
//             start,
//             end,
//             reason.c_str()
//         );
//         if (exc == nullptr) {
//             PyErr_SetString(PyExc_UnicodeError, message().c_str());
//         } else {
//             PyTracebackObject* tb = traceback.to_python();
//             if (tb != nullptr) {
//                 PyException_SetTraceback(exc, reinterpret_cast<PyObject*>(tb));
//             }
//             PyThreadState* thread = PyThreadState_Get();
//             thread->current_exception = exc;
//         }
//     }

// };


#undef BUILTIN_EXCEPTION


////////////////////////////////////
////    FORWARD DECLARATIONS    ////
////////////////////////////////////



inline auto __init__<Frame>::operator()() {
    PyFrameObject* frame = PyEval_GetFrame();
    if (frame == nullptr) {
        throw RuntimeError("no frame is currently executing");
    }
    return reinterpret_borrow<Frame>(reinterpret_cast<PyObject*>(frame));
}


template <std::convertible_to<int> T>
Frame __explicit_init__<Frame, T>::operator()(int skip) {
    PyFrameObject* frame = reinterpret_cast<PyFrameObject*>(
        Py_XNewRef(PyEval_GetFrame())
    );
    if (frame == nullptr) {
        throw RuntimeError("no frame is currently executing");
    }

    // negative indexing offsets from the most recent frame
    if (skip < 0) {
        for (int i = 0; i > skip; --i) {
            PyFrameObject* temp = PyFrame_GetBack(frame);
            if (temp == nullptr) {
                return reinterpret_steal<Frame>(reinterpret_cast<PyObject*>(frame));
            }
            Py_DECREF(frame);
            frame = temp;
        }
        return reinterpret_steal<Frame>(reinterpret_cast<PyObject*>(frame));
    }

    // positive indexing counts from the least recent frame
    std::vector<Frame> frames;
    while (frame != nullptr) {
        frames.push_back(reinterpret_steal<Frame>(
            reinterpret_cast<PyObject*>(frame))
        );
        frame = PyFrame_GetBack(frame);
    }
    if (skip >= frames.size()) {
        return frames.front();
    }
    return frames[skip];
}


inline auto __call__<Frame>::operator()(const Frame& frame) {
    PyObject* result = PyEval_EvalFrame(
        reinterpret_cast<PyFrameObject*>(ptr(frame))
    );
    if (result == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Object>(result);
}


[[nodiscard]] Frame Interface<Frame>::back() const {
    PyFrameObject* result = PyFrame_GetBack(
        reinterpret_cast<PyFrameObject*>(
            ptr(reinterpret_cast<const Object&>(*this))
        )
    );
    if (result == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Frame>(reinterpret_cast<PyObject*>(result));
}


[[nodiscard]] std::string Interface<Frame>::to_string() const {
    PyFrameObject* frame = reinterpret_cast<PyFrameObject*>(
        ptr(reinterpret_cast<const Object&>(*this))
    );
    PyCodeObject* code = PyFrame_GetCode(frame);

    std::string out;
    if (code != nullptr) {
        Py_ssize_t len;
        const char* name = PyUnicode_AsUTF8AndSize(code->co_filename, &len);
        if (name == nullptr) {
            Py_DECREF(code);
            Exception::from_python();
        }
        out += "File \"" + std::string(name, len) + "\", line ";
        out += std::to_string(PyFrame_GetLineNumber(frame)) + ", in ";
        name = PyUnicode_AsUTF8AndSize(code->co_name, &len);
        if (name == nullptr) {
            Py_DECREF(code);
            Exception::from_python();
        }
        out += std::string(name, len);
        Py_DECREF(code);
    } else {
        out += "File \"<unknown>\", line 0, in <unknown>";
    }

    return out;
}


[[nodiscard]] inline auto __iter__<Traceback>::operator*() const
    -> value_type
{
    if (curr == nullptr) {
        throw StopIteration();
    }
    return reinterpret_borrow<Frame>(
        reinterpret_cast<PyObject*>(curr->tb_frame)
    );
}


inline [[nodiscard]] auto __reversed__<Traceback>::operator*() const
    -> value_type
{
    if (index < 0) {
        throw StopIteration();
    }
    return reinterpret_borrow<Frame>(
        reinterpret_cast<PyObject*>(frames[index]->tb_frame)
    );
}




}  // namespace py


#endif
