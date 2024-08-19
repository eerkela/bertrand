#ifndef BERTRAND_PYTHON_CORE_EXCEPT_H
#define BERTRAND_PYTHON_CORE_EXCEPT_H

#include "declarations.h"
#include "object.h"
#include "code.h"


namespace py {


///////////////////////////
////    STACK TRACE    ////
///////////////////////////


namespace impl {

    inline const char* virtualenv = std::getenv("BERTRAND_HOME");

    inline PyTracebackObject* build_traceback(
        const cpptrace::stacktrace& trace,
        PyTracebackObject* front = nullptr
    ) {
        for (auto&& frame : trace) {
            // stop the traceback if we encounter a C++ frame in which a nested Python
            // script was executed.
            if (frame.symbol.find("py::Code::operator()") != std::string::npos) {
                break;

            // ignore frames that are not part of the user's code
            } else if (
                frame.symbol.starts_with("__") ||
                (virtualenv != nullptr && frame.filename.starts_with(virtualenv))
            ) {
                continue;

            } else {
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
                tb->tb_frame = reinterpret_cast<PyFrameObject*>(release(Frame(frame)));
                tb->tb_lasti = PyFrame_GetLasti(tb->tb_frame) * sizeof(_Py_CODEUNIT);
                tb->tb_lineno = PyFrame_GetLineNumber(tb->tb_frame);
                PyObject_GC_Track(tb);
                front = tb;
            }
        }
        return front;
    }

}


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
        PyTracebackObject* front = impl::build_traceback(trace);

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
                Py_DECREF(front);
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


/////////////////////////
////    EXCEPTION    ////
/////////////////////////


namespace impl {
    // short-circuits type imports for standard library exceptions to avoid circular
    // dependencies
    template <typename Exc>
    struct builtin_exception_map {};
}


struct Exception;


template <>
struct Interface<Exception> {
    [[noreturn, clang::noinline]] static void from_python();  // defined in __init__.h
    static void to_python();
};


struct Exception : std::exception, Object, Interface<Exception> {
protected:
    mutable std::optional<std::string> m_message;
    mutable std::optional<std::string> m_what;

    template <std::derived_from<Exception> T, typename... Args>
    Exception(implicit_ctor<T> ctor, Args&&... args) : Object(
        ctor, std::forward<Args>(args)...
    ) {}

    template <std::derived_from<Exception> T, typename... Args>
    Exception(explicit_ctor<T> ctor, Args&&... args) : Object(
        ctor, std::forward<Args>(args)...
    ) {}

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
        if (!m_message.has_value()) {
            PyObject* args = PyException_GetArgs(ptr(*this));
            if (args == nullptr) {
                PyErr_Clear();
                return "";
            } else if (PyTuple_GET_SIZE(args) == 0) {
                Py_DECREF(args);
                return "";
            }

            PyObject* msg = PyTuple_GET_ITEM(args, 0);
            if (msg == nullptr) {
                return "";
            }
            Py_ssize_t len;
            const char* result = PyUnicode_AsUTF8AndSize(msg, &len);
            if (result == nullptr) {
                PyErr_Clear();
                Py_DECREF(args);
                return "";
            }
            m_message = std::string(result, len);

            Py_DECREF(args);
        }
        return m_message.value().c_str();
    }

    /* Returns a Python-style traceback and error as a C++ string, which will be
    displayed in case of an uncaught error. */
    const char* what() const noexcept override {
        if (!m_what.has_value()) {
            std::string msg;
            Traceback tb = reinterpret_steal<Traceback>(
                PyException_GetTraceback(ptr(*this))
            );
            if (ptr(tb) != nullptr) {
                msg = tb.to_string() + "\n";
            } else {
                msg = "";
            }
            msg += Py_TYPE(ptr(*this))->tp_name;
            std::string this_message = message();
            if (!this_message.empty()) {
                msg += ": ";
                msg += this_message;
            }
            m_what = msg;
        }
        return m_what.value().c_str();
    }

    /* Clear the error's message() and what() caches, forcing them to be recomputed
    the next time they are requested. */
    void flush() const noexcept {
        m_message.reset();
        m_what.reset();
    }

};


template <std::derived_from<Exception> Exc, typename Msg>
    requires (std::convertible_to<std::decay_t<Msg>, std::string>)
struct __explicit_init__<Exc, Msg>                          : Returns<Exc> {
    [[clang::noinline]] static auto operator()(const std::string& msg) {
        PyObject* result;
        if constexpr (std::is_invocable_v<impl::builtin_exception_map<Exc>>) {
            result = PyObject_CallFunction(
                impl::builtin_exception_map<Exc>{}(),
                "s",
                msg.c_str()
            );
        } else {
            result = PyObject_CallFunction(
                ptr(Type<Exc>()),
                "s",
                msg.c_str()
            );
        }
        if (result == nullptr) {
            Exception::from_python();
        }

        // by default, the exception will have an empty traceback, so we need to
        // populate it with C++ frames if directed.
        #ifndef BERTRAND_NO_TRACEBACK
            try {
                PyTracebackObject* trace = impl::build_traceback(
                    cpptrace::generate_trace(1)
                );
                if (trace != nullptr) {
                    PyException_SetTraceback(result, reinterpret_cast<PyObject*>(trace));
                    Py_DECREF(trace);
                }
            } catch (...) {
                Py_DECREF(result);
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


inline void Interface<Exception>::to_python() {
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


#define BUILTIN_EXCEPTION(CLS, BASE, PYTYPE)                                            \
    struct CLS;                                                                         \
                                                                                        \
    template <>                                                                         \
    struct impl::builtin_exception_map<CLS> {                                           \
        [[nodiscard]] static PyObject* operator()() {                                   \
            return PYTYPE;                                                              \
        }                                                                               \
    };                                                                                  \
                                                                                        \
    template <>                                                                         \
    struct Interface<CLS> : Interface<BASE> {};                                         \
                                                                                        \
    struct CLS : Exception, Interface<CLS> {                                            \
        CLS(Handle h, borrowed_t t) : Exception(h, t) {}                                \
        CLS(Handle h, stolen_t t) : Exception(h, t) {}                                  \
                                                                                        \
        template <typename... Args>                                                     \
            requires (implicit_ctor<CLS>::template enable<Args...>)                     \
        [[clang::noinline]] CLS(Args&&... args) : Exception(                            \
            implicit_ctor<CLS>{},                                                       \
            std::forward<Args>(args)...                                                 \
        ) {}                                                                            \
                                                                                        \
        template <typename... Args>                                                     \
            requires (explicit_ctor<CLS>::template enable<Args...>)                     \
        [[clang::noinline]] explicit CLS(Args&&... args) : Exception(                   \
            explicit_ctor<CLS>{},                                                       \
            std::forward<Args>(args)...                                                 \
        ) {}                                                                            \
    };


BUILTIN_EXCEPTION(ArithmeticError, Exception, PyExc_ArithmeticError)
    BUILTIN_EXCEPTION(FloatingPointError, ArithmeticError, PyExc_FloatingPointError)
    BUILTIN_EXCEPTION(OverflowError, ArithmeticError, PyExc_OverflowError)
    BUILTIN_EXCEPTION(ZeroDivisionError, ArithmeticError, PyExc_ZeroDivisionError)
BUILTIN_EXCEPTION(AssertionError, Exception, PyExc_AssertionError)
BUILTIN_EXCEPTION(AttributeError, Exception, PyExc_AttributeError)
BUILTIN_EXCEPTION(BufferError, Exception, PyExc_BufferError)
BUILTIN_EXCEPTION(EOFError, Exception, PyExc_EOFError)
BUILTIN_EXCEPTION(ImportError, Exception, PyExc_ImportError)
    BUILTIN_EXCEPTION(ModuleNotFoundError, ImportError, PyExc_ModuleNotFoundError)
BUILTIN_EXCEPTION(LookupError, Exception, PyExc_LookupError)
    BUILTIN_EXCEPTION(IndexError, LookupError, PyExc_IndexError)
    BUILTIN_EXCEPTION(KeyError, LookupError, PyExc_KeyError)
BUILTIN_EXCEPTION(MemoryError, Exception, PyExc_MemoryError)
BUILTIN_EXCEPTION(NameError, Exception, PyExc_NameError)
    BUILTIN_EXCEPTION(UnboundLocalError, NameError, PyExc_UnboundLocalError)
BUILTIN_EXCEPTION(OSError, Exception, PyExc_OSError)
    BUILTIN_EXCEPTION(BlockingIOError, OSError, PyExc_BlockingIOError)
    BUILTIN_EXCEPTION(ChildProcessError, OSError, PyExc_ChildProcessError)
    BUILTIN_EXCEPTION(ConnectionError, OSError, PyExc_ConnectionError)
        BUILTIN_EXCEPTION(BrokenPipeError, ConnectionError, PyExc_BrokenPipeError)
        BUILTIN_EXCEPTION(ConnectionAbortedError, ConnectionError, PyExc_ConnectionAbortedError)
        BUILTIN_EXCEPTION(ConnectionRefusedError, ConnectionError, PyExc_ConnectionRefusedError)
        BUILTIN_EXCEPTION(ConnectionResetError, ConnectionError, PyExc_ConnectionResetError)
    BUILTIN_EXCEPTION(FileExistsError, OSError, PyExc_FileExistsError)
    BUILTIN_EXCEPTION(FileNotFoundError, OSError, PyExc_FileNotFoundError)
    BUILTIN_EXCEPTION(InterruptedError, OSError, PyExc_InterruptedError)
    BUILTIN_EXCEPTION(IsADirectoryError, OSError, PyExc_IsADirectoryError)
    BUILTIN_EXCEPTION(NotADirectoryError, OSError, PyExc_NotADirectoryError)
    BUILTIN_EXCEPTION(PermissionError, OSError, PyExc_PermissionError)
    BUILTIN_EXCEPTION(ProcessLookupError, OSError, PyExc_ProcessLookupError)
    BUILTIN_EXCEPTION(TimeoutError, OSError, PyExc_TimeoutError)
BUILTIN_EXCEPTION(ReferenceError, Exception, PyExc_ReferenceError)
BUILTIN_EXCEPTION(RuntimeError, Exception, PyExc_RuntimeError)
    BUILTIN_EXCEPTION(NotImplementedError, RuntimeError, PyExc_NotImplementedError)
    BUILTIN_EXCEPTION(RecursionError, RuntimeError, PyExc_RecursionError)
BUILTIN_EXCEPTION(StopAsyncIteration, Exception, PyExc_StopAsyncIteration)
BUILTIN_EXCEPTION(StopIteration, Exception, PyExc_StopIteration)
BUILTIN_EXCEPTION(SyntaxError, Exception, PyExc_SyntaxError)
    BUILTIN_EXCEPTION(IndentationError, SyntaxError, PyExc_IndentationError)
        BUILTIN_EXCEPTION(TabError, IndentationError, PyExc_TabError)
BUILTIN_EXCEPTION(SystemError, Exception, PyExc_SystemError)
BUILTIN_EXCEPTION(TypeError, Exception, PyExc_TypeError)
BUILTIN_EXCEPTION(ValueError, Exception, PyExc_ValueError)
    BUILTIN_EXCEPTION(UnicodeError, ValueError, PyExc_UnicodeError)
        // BUILTIN_EXCEPTION(UnicodeDecodeError, UnicodeError)
        // BUILTIN_EXCEPTION(UnicodeEncodeError, UnicodeError)
        // BUILTIN_EXCEPTION(UnicodeTranslateError, UnicodeError)

#undef BUILTIN_EXCEPTION


struct UnicodeDecodeError;


template <>
struct impl::builtin_exception_map<UnicodeDecodeError> {
    [[nodiscard]] static PyObject* operator()() {
        return PyExc_UnicodeDecodeError;
    }
};


template <>
struct Interface<UnicodeDecodeError> : Interface<UnicodeError> {
    __declspec(property(get=_encoding)) std::string encoding;
    [[nodiscard]] std::string _encoding() const;

    __declspec(property(get=_object)) std::string object;
    [[nodiscard]] std::string _object() const;

    __declspec(property(get=_start)) Py_ssize_t start;
    [[nodiscard]] Py_ssize_t _start() const;

    __declspec(property(get=_end)) Py_ssize_t end;
    [[nodiscard]] Py_ssize_t _end() const;

    __declspec(property(get=_reason)) std::string reason;
    [[nodiscard]] std::string _reason() const;
};


struct UnicodeDecodeError : Exception, Interface<UnicodeDecodeError> {

    UnicodeDecodeError(Handle h, borrowed_t t) : Exception(h, t) {}
    UnicodeDecodeError(Handle h, stolen_t t) : Exception(h, t) {}

    template <typename... Args>
        requires (implicit_ctor<UnicodeDecodeError>::template enable<Args...>)
    [[clang::noinline]] UnicodeDecodeError(Args&&... args) : Exception(
        implicit_ctor<UnicodeDecodeError>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args>
        requires (explicit_ctor<UnicodeDecodeError>::template enable<Args...>)
    [[clang::noinline]] explicit UnicodeDecodeError(Args&&... args) : Exception(
        explicit_ctor<UnicodeDecodeError>{},
        std::forward<Args>(args)...
    ) {}

    const char* message() const noexcept {
        if (!m_message.has_value()) {
            try {
                m_message =
                    "'" + encoding + "' codec can't decode bytes in position " +
                    std::to_string(start) + "-" + std::to_string(end - 1) +
                    ": " + reason;
            } catch (...) {
                return "";
            }
        }
        return m_message.value().c_str();
    }

    const char* what() const noexcept override {
        if (!m_what.has_value()) {
            try {
                std::string msg;
                Traceback tb = reinterpret_steal<Traceback>(
                    PyException_GetTraceback(ptr(*this))
                );
                if (ptr(tb) != nullptr) {
                    msg = tb.to_string() + "\n";
                } else {
                    msg = "";
                }
                msg += Py_TYPE(ptr(*this))->tp_name;
                std::string this_message = message();
                if (!this_message.empty()) {
                    msg += ": ";
                    msg += this_message;
                }
                m_what = msg;
            } catch (...) {
                return "";
            }
        }
        return m_what.value().c_str();
    }

};


template <>
struct __init__<UnicodeDecodeError>                         : Disable {};
template <typename Msg> requires (std::convertible_to<std::decay_t<Msg>, std::string>)
struct __explicit_init__<UnicodeDecodeError, Msg>           : Disable {};


template <
    typename Encoding,
    typename Obj,
    std::convertible_to<Py_ssize_t> Start,
    std::convertible_to<Py_ssize_t> End,
    typename Reason
>
    requires (
        std::convertible_to<std::decay_t<Encoding>, std::string> &&
        std::convertible_to<std::decay_t<Obj>, std::string> &&
        std::convertible_to<std::decay_t<Reason>, std::string>
    )
struct __explicit_init__<UnicodeDecodeError, Encoding, Obj, Start, End, Reason> :
    Returns<UnicodeDecodeError>
{
    [[clang::noinline]] static auto operator()(
        const std::string& encoding,
        const std::string& object,
        Py_ssize_t start,
        Py_ssize_t end,
        const std::string& reason
    ) {
        PyObject* result = PyUnicodeDecodeError_Create(
            encoding.c_str(),
            object.c_str(),
            object.size(),
            start,
            end,
            reason.c_str()
        );
        if (result == nullptr) {
            Exception::from_python();
        }

        #ifndef BERTRAND_NO_TRACEBACK
            try {
                PyTracebackObject* trace = impl::build_traceback(
                    cpptrace::generate_trace(1)
                );
                if (trace != nullptr) {
                    PyException_SetTraceback(result, reinterpret_cast<PyObject*>(trace));
                    Py_DECREF(trace);
                }
            } catch (...) {
                Py_DECREF(result);
                throw;
            }
        #endif

        return reinterpret_steal<UnicodeDecodeError>(result);
    }
};


[[nodiscard]] inline std::string Interface<UnicodeDecodeError>::_encoding() const {
    PyObject* encoding = PyUnicodeDecodeError_GetEncoding(
        ptr(reinterpret_cast<const Object&>(*this))
    );
    if (encoding == nullptr) {
        Exception::from_python();
    }
    Py_ssize_t len;
    const char* data = PyUnicode_AsUTF8AndSize(encoding, &len);
    if (data == nullptr) {
        Py_DECREF(encoding);
        Exception::from_python();
    }
    std::string result(data, len);
    Py_DECREF(encoding);
    return result;
}


[[nodiscard]] inline std::string Interface<UnicodeDecodeError>::_object() const {
    PyObject* object = PyUnicodeDecodeError_GetObject(
        ptr(reinterpret_cast<const Object&>(*this))
    );
    if (object == nullptr) {
        Exception::from_python();
    }
    Py_ssize_t len;
    char* data;
    if (PyBytes_AsStringAndSize(object, &data, &len)) {
        Py_DECREF(object);
        Exception::from_python();
    }
    std::string result(data, len);
    Py_DECREF(object);
    return result;
}


[[nodiscard]] inline Py_ssize_t Interface<UnicodeDecodeError>::_start() const {
    Py_ssize_t start;
    if (PyUnicodeDecodeError_GetStart(
        ptr(reinterpret_cast<const Object&>(*this)),
        &start
    )) {
        Exception::from_python();
    }
    return start;
}


[[nodiscard]] inline Py_ssize_t Interface<UnicodeDecodeError>::_end() const {
    Py_ssize_t end;
    if (PyUnicodeDecodeError_GetEnd(
        ptr(reinterpret_cast<const Object&>(*this)),
        &end
    )) {
        Exception::from_python();
    }
    return end;
}


[[nodiscard]] inline std::string Interface<UnicodeDecodeError>::_reason() const {
    PyObject* reason = PyUnicodeDecodeError_GetReason(
        ptr(reinterpret_cast<const Object&>(*this))
    );
    if (reason == nullptr) {
        Exception::from_python();
    }
    Py_ssize_t len;
    const char* data = PyUnicode_AsUTF8AndSize(reason, &len);
    if (data == nullptr) {
        Py_DECREF(reason);
        Exception::from_python();
    }
    std::string result(data, len);
    Py_DECREF(reason);
    return result;
}


struct UnicodeEncodeError;


template <>
struct impl::builtin_exception_map<UnicodeEncodeError> {
    [[nodiscard]] static PyObject* operator()() {
        return PyExc_UnicodeEncodeError;
    }
};


template <>
struct Interface<UnicodeEncodeError> : Interface<UnicodeError> {
    __declspec(property(get=_encoding)) std::string encoding;
    [[nodiscard]] std::string _encoding() const;

    __declspec(property(get=_object)) std::string object;
    [[nodiscard]] std::string _object() const;

    __declspec(property(get=_start)) Py_ssize_t start;
    [[nodiscard]] Py_ssize_t _start() const;

    __declspec(property(get=_end)) Py_ssize_t end;
    [[nodiscard]] Py_ssize_t _end() const;

    __declspec(property(get=_reason)) std::string reason;
    [[nodiscard]] std::string _reason() const;
};


struct UnicodeEncodeError : Exception, Interface<UnicodeEncodeError> {

    UnicodeEncodeError(Handle h, borrowed_t t) : Exception(h, t) {}
    UnicodeEncodeError(Handle h, stolen_t t) : Exception(h, t) {}

    template <typename... Args>
        requires (implicit_ctor<UnicodeEncodeError>::template enable<Args...>)
    [[clang::noinline]] UnicodeEncodeError(Args&&... args) : Exception(
        implicit_ctor<UnicodeEncodeError>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args>
        requires (explicit_ctor<UnicodeEncodeError>::template enable<Args...>)
    [[clang::noinline]] explicit UnicodeEncodeError(Args&&... args) : Exception(
        explicit_ctor<UnicodeEncodeError>{},
        std::forward<Args>(args)...
    ) {}

    const char* message() const noexcept {
        if (!m_message.has_value()) {
            try {
                m_message =
                    "'" + encoding + "' codec can't encode characters in position " +
                    std::to_string(start) + "-" + std::to_string(end - 1) +
                    ": " + reason;
            } catch (...) {
                return "";
            }
        }
        return m_message.value().c_str();
    }

    const char* what() const noexcept override {
        if (!m_what.has_value()) {
            try {
                std::string msg;
                Traceback tb = reinterpret_steal<Traceback>(
                    PyException_GetTraceback(ptr(*this))
                );
                if (ptr(tb) != nullptr) {
                    msg = tb.to_string() + "\n";
                } else {
                    msg = "";
                }
                msg += Py_TYPE(ptr(*this))->tp_name;
                std::string this_message = message();
                if (!this_message.empty()) {
                    msg += ": ";
                    msg += this_message;
                }
                m_what = msg;
            } catch (...) {
                return "";
            }
        }
        return m_what.value().c_str();
    }

};


template <>
struct __init__<UnicodeEncodeError>                         : Disable {};
template <typename Msg> requires (std::convertible_to<std::decay_t<Msg>, std::string>)
struct __explicit_init__<UnicodeEncodeError, Msg>           : Disable {};


template <
    typename Encoding,
    typename Obj,
    std::convertible_to<Py_ssize_t> Start,
    std::convertible_to<Py_ssize_t> End,
    typename Reason
>
    requires (
        std::convertible_to<std::decay_t<Encoding>, std::string> &&
        std::convertible_to<std::decay_t<Obj>, std::string> &&
        std::convertible_to<std::decay_t<Reason>, std::string>
    )
struct __explicit_init__<UnicodeEncodeError, Encoding, Obj, Start, End, Reason> :
    Returns<UnicodeEncodeError>
{
    [[clang::noinline]] static auto operator()(
        const std::string& encoding,
        const std::string& object,
        Py_ssize_t start,
        Py_ssize_t end,
        const std::string& reason
    ) {
        PyObject* result = PyObject_CallFunction(
            PyExc_UnicodeEncodeError,
            "ssnns",
            encoding.c_str(),
            object.c_str(),
            start,
            end,
            reason.c_str()
        );
        if (result == nullptr) {
            Exception::from_python();
        }

        #ifndef BERTRAND_NO_TRACEBACK
            try {
                PyTracebackObject* trace = impl::build_traceback(
                    cpptrace::generate_trace(1)
                );
                if (trace != nullptr) {
                    PyException_SetTraceback(result, reinterpret_cast<PyObject*>(trace));
                    Py_DECREF(trace);
                }
            } catch (...) {
                Py_DECREF(result);
                throw;
            }
        #endif

        return reinterpret_steal<UnicodeEncodeError>(result);
    }
};


[[nodiscard]] inline std::string Interface<UnicodeEncodeError>::_encoding() const {
    PyObject* encoding = PyUnicodeEncodeError_GetEncoding(
        ptr(reinterpret_cast<const Object&>(*this))
    );
    if (encoding == nullptr) {
        Exception::from_python();
    }
    Py_ssize_t len;
    const char* data = PyUnicode_AsUTF8AndSize(encoding, &len);
    if (data == nullptr) {
        Py_DECREF(encoding);
        Exception::from_python();
    }
    std::string result(data, len);
    Py_DECREF(encoding);
    return result;
}


[[nodiscard]] inline std::string Interface<UnicodeEncodeError>::_object() const {
    PyObject* object = PyUnicodeEncodeError_GetObject(
        ptr(reinterpret_cast<const Object&>(*this))
    );
    if (object == nullptr) {
        Exception::from_python();
    }
    Py_ssize_t len;
    const char* data = PyUnicode_AsUTF8AndSize(object, &len);
    if (data == nullptr) {
        Py_DECREF(object);
        Exception::from_python();
    }
    std::string result(data, len);
    Py_DECREF(object);
    return result;
}


[[nodiscard]] inline Py_ssize_t Interface<UnicodeEncodeError>::_start() const {
    Py_ssize_t start;
    if (PyUnicodeEncodeError_GetStart(
        ptr(reinterpret_cast<const Object&>(*this)),
        &start
    )) {
        Exception::from_python();
    }
    return start;
}


[[nodiscard]] inline Py_ssize_t Interface<UnicodeEncodeError>::_end() const {
    Py_ssize_t end;
    if (PyUnicodeEncodeError_GetEnd(
        ptr(reinterpret_cast<const Object&>(*this)),
        &end
    )) {
        Exception::from_python();
    }
    return end;
}


[[nodiscard]] inline std::string Interface<UnicodeEncodeError>::_reason() const {
    PyObject* reason = PyUnicodeEncodeError_GetReason(
        ptr(reinterpret_cast<const Object&>(*this))
    );
    if (reason == nullptr) {
        Exception::from_python();
    }
    Py_ssize_t len;
    const char* data = PyUnicode_AsUTF8AndSize(reason, &len);
    if (data == nullptr) {
        Py_DECREF(reason);
        Exception::from_python();
    }
    std::string result(data, len);
    Py_DECREF(reason);
    return result;
}


struct UnicodeTranslateError;


template <>
struct impl::builtin_exception_map<UnicodeTranslateError> {
    [[nodiscard]] static PyObject* operator()() {
        return PyExc_UnicodeTranslateError;
    }
};


template <>
struct Interface<UnicodeTranslateError> : Interface<UnicodeError> {
    __declspec(property(get=_object)) std::string object;
    [[nodiscard]] std::string _object() const;

    __declspec(property(get=_start)) Py_ssize_t start;
    [[nodiscard]] Py_ssize_t _start() const;

    __declspec(property(get=_end)) Py_ssize_t end;
    [[nodiscard]] Py_ssize_t _end() const;

    __declspec(property(get=_reason)) std::string reason;
    [[nodiscard]] std::string _reason() const;
};


struct UnicodeTranslateError : Exception, Interface<UnicodeTranslateError> {

    UnicodeTranslateError(Handle h, borrowed_t t) : Exception(h, t) {}
    UnicodeTranslateError(Handle h, stolen_t t) : Exception(h, t) {}

    template <typename... Args>
        requires (implicit_ctor<UnicodeTranslateError>::template enable<Args...>)
    [[clang::noinline]] UnicodeTranslateError(Args&&... args) : Exception(
        implicit_ctor<UnicodeTranslateError>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args>
        requires (explicit_ctor<UnicodeTranslateError>::template enable<Args...>)
    [[clang::noinline]] explicit UnicodeTranslateError(Args&&... args) : Exception(
        explicit_ctor<UnicodeTranslateError>{},
        std::forward<Args>(args)...
    ) {}

    const char* message() const noexcept {
        if (!m_message.has_value()) {
            try {
                m_message =
                    "can't translate characters in position " + std::to_string(start) +
                    "-" + std::to_string(end - 1) + ": " + reason;
            } catch (...) {
                return "";
            }
        }
        return m_message.value().c_str();
    }

    const char* what() const noexcept override {
        if (!m_what.has_value()) {
            try {
                std::string msg;
                Traceback tb = reinterpret_steal<Traceback>(
                    PyException_GetTraceback(ptr(*this))
                );
                if (ptr(tb) != nullptr) {
                    msg = tb.to_string() + "\n";
                } else {
                    msg = "";
                }
                msg += Py_TYPE(ptr(*this))->tp_name;
                std::string this_message = message();
                if (!this_message.empty()) {
                    msg += ": ";
                    msg += this_message;
                }
                m_what = msg;
            } catch (...) {
                return "";
            }
        }
        return m_what.value().c_str();
    }

};


template <>
struct __init__<UnicodeTranslateError>                      : Disable {};
template <typename Msg> requires (std::convertible_to<std::decay_t<Msg>, std::string>)
struct __explicit_init__<UnicodeTranslateError, Msg>        : Disable {};


template <
    typename Encoding,
    typename Obj,
    std::convertible_to<Py_ssize_t> Start,
    std::convertible_to<Py_ssize_t> End,
    typename Reason
>
    requires (
        std::convertible_to<std::decay_t<Encoding>, std::string> &&
        std::convertible_to<std::decay_t<Obj>, std::string> &&
        std::convertible_to<std::decay_t<Reason>, std::string>
    )
struct __explicit_init__<UnicodeTranslateError, Encoding, Obj, Start, End, Reason> :
    Returns<UnicodeTranslateError>
{
    [[clang::noinline]] static auto operator()(
        const std::string& object,
        Py_ssize_t start,
        Py_ssize_t end,
        const std::string& reason
    ) {
        PyObject* result = PyObject_CallFunction(
            PyExc_UnicodeTranslateError,
            "snns",
            object.c_str(),
            start,
            end,
            reason.c_str()
        );
        if (result == nullptr) {
            Exception::from_python();
        }

        #ifndef BERTRAND_NO_TRACEBACK
            try {
                PyTracebackObject* trace = impl::build_traceback(
                    cpptrace::generate_trace(1)
                );
                if (trace != nullptr) {
                    PyException_SetTraceback(result, reinterpret_cast<PyObject*>(trace));
                    Py_DECREF(trace);
                }
            } catch (...) {
                Py_DECREF(result);
                throw;
            }
        #endif

        return reinterpret_steal<UnicodeTranslateError>(result);
    }
};


[[nodiscard]] inline std::string Interface<UnicodeTranslateError>::_object() const {
    PyObject* object = PyUnicodeTranslateError_GetObject(
        ptr(reinterpret_cast<const Object&>(*this))
    );
    if (object == nullptr) {
        Exception::from_python();
    }
    Py_ssize_t len;
    const char* data = PyUnicode_AsUTF8AndSize(object, &len);
    if (data == nullptr) {
        Py_DECREF(object);
        Exception::from_python();
    }
    std::string result(data, len);
    Py_DECREF(object);
    return result;
}


[[nodiscard]] inline Py_ssize_t Interface<UnicodeTranslateError>::_start() const {
    Py_ssize_t start;
    if (PyUnicodeTranslateError_GetStart(
        ptr(reinterpret_cast<const Object&>(*this)),
        &start
    )) {
        Exception::from_python();
    }
    return start;
}


[[nodiscard]] inline Py_ssize_t Interface<UnicodeTranslateError>::_end() const {
    Py_ssize_t end;
    if (PyUnicodeTranslateError_GetEnd(
        ptr(reinterpret_cast<const Object&>(*this)),
        &end
    )) {
        Exception::from_python();
    }
    return end;
}


[[nodiscard]] inline std::string Interface<UnicodeTranslateError>::_reason() const {
    PyObject* reason = PyUnicodeTranslateError_GetReason(
        ptr(reinterpret_cast<const Object&>(*this))
    );
    if (reason == nullptr) {
        Exception::from_python();
    }
    Py_ssize_t len;
    const char* data = PyUnicode_AsUTF8AndSize(reason, &len);
    if (data == nullptr) {
        Py_DECREF(reason);
        Exception::from_python();
    }
    std::string result(data, len);
    Py_DECREF(reason);
    return result;
}


//////////////////////
////    OBJECT    ////
//////////////////////


template <typename Self, typename Key> requires (__contains__<Self, Key>::enable)
[[nodiscard]] bool Handle::contains(this const Self& self, const Key& key) {
    using Return = typename __contains__<Self, Key>::type;
    static_assert(
        std::same_as<Return, bool>,
        "contains() operator must return a boolean value.  Check your "
        "specialization of __contains__ for these types and ensure the Return "
        "type is set to bool."
    );
    if constexpr (impl::has_call_operator<__contains__<Self, Key>>) {
        return __contains__<Self, Key>{}(self, key);

    } else if constexpr (
        impl::originates_from_cpp<Self> &&
        impl::cpp_or_originates_from_cpp<Key>
    ) {
        static_assert(
            impl::has_contains<impl::cpp_type<Self>, impl::cpp_type<Key>>,
            "__contains__<Self, Key> is enabled for operands whose C++ "
            "representations have no viable overload for `Self.contains(Key)`"
        );
        return unwrap(self).contains(unwrap(key));

    } else {
        int result = PySequence_Contains(
            self.m_ptr,
            ptr(as_object(key))
        );
        if (result == -1) {
            Exception::from_python();
        }
        return result;
    }
}


/// TODO: should these operators go in ops.h?


template <typename Self>
[[nodiscard]] Handle::operator bool(this const Self& self) {
    if constexpr (
        impl::originates_from_cpp<Self> &&
        impl::has_operator_bool<impl::cpp_type<Self>>
    ) {
        return static_cast<bool>(unwrap(self));
    } else {
        int result = PyObject_IsTrue(self.m_ptr);
        if (result == -1) {
            Exception::from_python();
        }
        return result;   
    }
}


template <typename T>
constexpr bool __isinstance__<T, Object>::operator()(const T& obj, const Object& cls) {
    if constexpr (impl::python_like<T>) {
        int result = PyObject_IsInstance(
            ptr(obj),
            ptr(cls)
        );
        if (result < 0) {
            Exception::from_python();
        }
        return result;
    } else {
        return false;
    }
}


template <typename T>
bool __issubclass__<T, Object>::operator()(const T& obj, const Object& cls) {
    int result = PyObject_IsSubclass(
        ptr(as_object(obj)),
        ptr(cls)
    );
    if (result == -1) {
        Exception::from_python();
    }
    return result;
}


template <std::derived_from<Handle> From, std::derived_from<From> To>
auto __cast__<From, To>::operator()(const From& from) {
    if (isinstance<To>(from)) {
        return reinterpret_borrow<To>(ptr(from));
    } else {
        throw TypeError(
            "cannot convert Python object from type '" + repr(Type<From>()) +
            "' to type '" + repr(Type<To>()) + "'"
        );
    }
}


template <std::derived_from<Handle> From, std::derived_from<From> To>
auto __cast__<From, To>::operator()(From&& from) {
    if (isinstance<To>(from)) {
        return reinterpret_steal<To>(release(from));
    } else {
        throw TypeError(
            "cannot convert Python object from type '" + repr(Type<From>()) +
            "' to type '" + repr(Type<To>()) + "'"
        );
    }
}


template <std::derived_from<Handle> From, std::integral To>
To __explicit_cast__<From, To>::operator()(const From& from) {
    long long result = PyLong_AsLongLong(ptr(from));
    if (result == -1 && PyErr_Occurred()) {
        Exception::from_python();
    } else if (
        result < std::numeric_limits<To>::min() ||
        result > std::numeric_limits<To>::max()
    ) {
        throw OverflowError(
            "integer out of range for " + impl::demangle(typeid(To).name()) +
            ": " + std::to_string(result)
        );
    }
    return result;
}


template <std::derived_from<Handle> From, std::floating_point To>
To __explicit_cast__<From, To>::operator()(const From& from) {
    double result = PyFloat_AsDouble(ptr(from));
    if (result == -1.0 && PyErr_Occurred()) {
        Exception::from_python();
    }
    return result;
}

template <std::derived_from<Handle> From, impl::complex_like To>
    requires (impl::cpp_like<To>)
To __explicit_cast__<From, To>::operator()(const From& from) {
    Py_complex result = PyComplex_AsCComplex(ptr(from));
    if (result.real == -1.0 && PyErr_Occurred()) {
        Exception::from_python();
    }
    return To(result.real, result.imag);
}


template <std::derived_from<Handle> From>
auto __explicit_cast__<From, std::string>::operator()(const From& from) {
    PyObject* str = PyObject_Str(ptr(from));
    if (str == nullptr) {
        Exception::from_python();
    }
    Py_ssize_t size;
    const char* data = PyUnicode_AsUTF8AndSize(str, &size);
    if (data == nullptr) {
        Py_DECREF(str);
        Exception::from_python();
    }
    std::string result(data, size);
    Py_DECREF(str);
    return result;
}


////////////////////
////    CODE    ////
////////////////////


template <typename Source>
    requires (std::convertible_to<std::decay_t<Source>, std::string>)
auto __init__<Code, Source>::operator()(const std::string& source) {
    std::string line;
    std::string parsed;
    std::istringstream stream(source);
    size_t min_indent = std::numeric_limits<size_t>::max();

    // find minimum indentation
    while (std::getline(stream, line)) {
        if (line.empty()) {
            continue;
        }
        size_t indent = line.find_first_not_of(" \t");
        if (indent != std::string::npos) {
            min_indent = std::min(min_indent, indent);
        }
    }

    // dedent if necessary
    if (min_indent != std::numeric_limits<size_t>::max()) {
        std::string temp;
        std::istringstream stream2(source);
        while (std::getline(stream2, line)) {
            if (line.empty() || line.find_first_not_of(" \t") == std::string::npos) {
                temp += '\n';
            } else {
                temp += line.substr(min_indent) + '\n';
            }
        }
        parsed = temp;
    } else {
        parsed = source;
    }

    PyObject* result = Py_CompileString(
        parsed.c_str(),
        "<embedded Python script>",
        Py_file_input
    );
    if (result == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Code>(result);
}



/* Parse and compile a source file into a Python code object. */
[[nodiscard]] inline Code Interface<Code>::compile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw FileNotFoundError(std::string("'") + path + "'");
    }
    std::istreambuf_iterator<char> begin(file), end;
    PyObject* result = Py_CompileString(
        std::string(begin, end).c_str(),
        path.c_str(),
        Py_file_input
    );
    if (result == nullptr) {
        Exception::from_python();
    }
    return reinterpret_steal<Code>(result);
}



/////////////////////
////    FRAME    ////
/////////////////////


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


[[nodiscard]] inline std::string Interface<Frame>::to_string() const {
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


[[nodiscard]] inline std::optional<Code> Interface<Frame>::_code() const {
    PyCodeObject* code = PyFrame_GetCode(
        reinterpret_cast<PyFrameObject*>(
            ptr(reinterpret_cast<const Object&>(*this))
        )
    );
    if (code == nullptr) {
        return std::nullopt;
    }
    return reinterpret_steal<Code>(reinterpret_cast<PyObject*>(code));
}


[[nodiscard]] inline std::optional<Frame> Interface<Frame>::_back() const {
    PyFrameObject* result = PyFrame_GetBack(
        reinterpret_cast<PyFrameObject*>(
            ptr(reinterpret_cast<const Object&>(*this))
        )
    );
    if (result == nullptr) {
        return std::nullopt;
    }
    return reinterpret_steal<Frame>(reinterpret_cast<PyObject*>(result));
}


[[nodiscard]] inline size_t Interface<Frame>::_line_number() const {
    return PyFrame_GetLineNumber(
        reinterpret_cast<PyFrameObject*>(
            ptr(reinterpret_cast<const Object&>(*this))
        )
    );
}


[[nodiscard]] inline size_t Interface<Frame>::_last_instruction() const {
    int result = PyFrame_GetLasti(
        reinterpret_cast<PyFrameObject*>(
            ptr(reinterpret_cast<const Object&>(*this))
        )
    );
    if (result < 0) {
        throw RuntimeError("frame is not currently executing");
    }
    return result;
}


[[nodiscard]] inline std::optional<Object> Interface<Frame>::_generator() const {
    PyObject* result = PyFrame_GetGenerator(
        reinterpret_cast<PyFrameObject*>(
            ptr(reinterpret_cast<const Object&>(*this))
        )
    );
    if (result == nullptr) {
        return std::nullopt;
    }
    return reinterpret_steal<Object>(result);
}


/////////////////////////
////    TRACEBACK    ////
/////////////////////////


[[nodiscard]] inline auto __iter__<Traceback>::operator*() const -> value_type {
    if (curr == nullptr) {
        throw StopIteration();
    }
    return reinterpret_borrow<Frame>(
        reinterpret_cast<PyObject*>(curr->tb_frame)
    );
}


[[nodiscard]] inline auto __reversed__<Traceback>::operator*() const -> value_type {
    if (index < 0) {
        throw StopIteration();
    }
    return reinterpret_borrow<Frame>(
        reinterpret_cast<PyObject*>(frames[index]->tb_frame)
    );
}


[[nodiscard]] inline std::string Interface<Traceback>::to_string() const {
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


}  // namespace py


#endif
