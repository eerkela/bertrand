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
struct interface<Traceback> {
    [[nodiscard]] std::string to_string(this const auto& self);
};


/* A cross-language traceback that records an accurate call stack of a mixed Python/C++
application. */
struct Traceback : Object, interface<Traceback> {
    struct __python__ : def<__python__, Traceback>, PyTracebackObject {
        static Type<Traceback> __import__();
    };

    Traceback(PyObject* p, borrowed_t t) : Object(p, t) {}
    Traceback(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename T = Traceback> requires (__initializer__<T>::enable)
    [[clang::noinline]] Traceback(
        std::initializer_list<typename __initializer__<T>::type> init
    ) : Object(__initializer__<T>{}(init)) {}

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


template <>
struct interface<Type<Traceback>> {
    [[nodiscard]] static std::string to_string(const auto& self) {
        return self.to_string();
    }
};


template <impl::is<cpptrace::stacktrace> T>
struct __cast__<T>                                          : returns<Traceback> {};


/* Converting a `cpptrace::stacktrace_frame` into a Python frame object will synthesize
an interpreter frame with an empty bytecode object. */
template <impl::is<cpptrace::stacktrace> T>
struct __cast__<T, Traceback>                               : returns<Traceback> {
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
struct __init__<Traceback>                                  : returns<Traceback> {
    [[clang::noinline]] static auto operator()() {
        return Traceback(cpptrace::generate_trace(1));
    }
};


/* Providing an explicit integer will skip that number of frames from either the least
recent frame (if positive or zero) or the most recent (if negative).  Positive integers
will produce a traceback with at most the given length, and negative integers will
reduce the length by at most the given value. */
template <std::convertible_to<int> T>
struct __init__<Traceback, T>                               : returns<Traceback> {
    static auto operator()(int skip) {
        // if skip is zero, then the result will be empty by definition
        if (skip == 0) {
            return reinterpret_steal<Traceback>(nullptr);
        }

        // compute the full traceback to account for mixed C++ and Python frames
        Traceback trace(cpptrace::generate_trace(1));
        PyTracebackObject* curr = reinterpret_cast<PyTracebackObject*>(ptr(trace));

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
template <impl::is<Traceback> Self>
struct __len__<Self>                                        : returns<size_t> {
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
template <impl::is<Traceback> Self>
struct __iter__<Self>                                       : returns<Frame> {
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = Frame;
    using reference = Frame&;
    using pointer = Frame*;

    Traceback traceback;
    PyTracebackObject* curr;

    __iter__(const Traceback& self) :
        traceback(self),
        curr(reinterpret_cast<PyTracebackObject*>(ptr(traceback)))
    {}
    __iter__(Traceback&& self) :
        traceback(std::move(self)),
        curr(reinterpret_cast<PyTracebackObject*>(ptr(traceback)))
    {}
    __iter__(const __iter__& other) : traceback(other.traceback), curr(other.curr) {}
    __iter__(__iter__&& other) : traceback(std::move(other.traceback)), curr(other.curr) {
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

    [[nodiscard]] Frame operator*() const;

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

    [[nodiscard]] friend bool operator==(const __iter__& self, sentinel) {
        return self.curr == nullptr;
    }

    [[nodiscard]] friend bool operator==(sentinel, const __iter__& self) {
        return self.curr == nullptr;
    }

    [[nodiscard]] friend bool operator!=(const __iter__& self, sentinel) {
        return self.curr != nullptr;
    }

    [[nodiscard]] friend bool operator!=(sentinel, const __iter__& self) {
        return self.curr != nullptr;
    }
};


/* Reverse iterating over the frames yields them in most recent -> least recent order. */
template <impl::is<Traceback> Self>
struct __reversed__<Self>                                   : returns<Traceback> {
    using iterator_category = std::forward_iterator_tag;
    using difference_type = std::ptrdiff_t;
    using value_type = Frame;
    using reference = Frame&;
    using pointer = Frame*;

    Traceback traceback;
    std::vector<PyTracebackObject*> frames;
    Py_ssize_t index;

    __reversed__(const Traceback& self) : traceback(self) {
        PyTracebackObject* curr = reinterpret_cast<PyTracebackObject*>(
            ptr(traceback)
        );
        while (curr != nullptr) {
            frames.push_back(curr);
            curr = curr->tb_next;
        }
        index = std::ssize(frames) - 1;
    }

    __reversed__(Traceback&& self) : traceback(std::move(self)) {
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

    [[nodiscard]] friend bool operator==(const __reversed__& self, sentinel) {
        return self.index == -1;
    }

    [[nodiscard]] friend bool operator==(sentinel, const __reversed__& self) {
        return self.index == -1;
    }

    [[nodiscard]] friend bool operator!=(const __reversed__& self, sentinel) {
        return self.index != -1;
    }

    [[nodiscard]] friend bool operator!=(sentinel, const __reversed__& self) {
        return self.index != -1;
    }
};


/////////////////////////
////    EXCEPTION    ////
/////////////////////////


struct Exception;


namespace impl {
    // short-circuits type imports for standard library exceptions to avoid circular
    // dependencies
    template <typename Exc>
    struct builtin_exception_map {};
}


template <>
struct interface<Exception> {
    [[noreturn, clang::noinline]] static void from_python();  // defined in __init__.h
    static void to_python();
};


/* The base of the exception hierarchy, from which all exceptions derive.  Exception
types should inherit from this class instead of `py::Object` in order to register a
new exception.  Otherwise, all the same semantics apply. */
struct Exception : std::exception, Object, interface<Exception> {
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
    struct __python__ : def<__python__, Exception>, PyBaseExceptionObject {
        static Type<Exception> __import__();
    };

    Exception(PyObject* p, borrowed_t t) : Object(p, t) {}
    Exception(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename T = Exception> requires (__initializer__<T>::enable)
    [[clang::noinline]] Exception(
        const std::initializer_list<typename __initializer__<T>::type>& init
    ) : Object(__initializer__<T>{}(init)) {}

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
        return m_message.value().data();
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
        return m_what.value().data();
    }

    /* Clear the error's message() and what() caches, forcing them to be recomputed
    the next time they are requested. */
    void flush() const noexcept {
        m_message.reset();
        m_what.reset();
    }

};


template <>
struct interface<Type<Exception>> {
    [[noreturn, clang::noinline]] static void from_python();  // defined in __init__.h
    static void to_python() {
        Exception::to_python();
    }
};


template <std::derived_from<Exception> Exc, typename Msg>
    requires (std::convertible_to<Msg, std::string_view> || impl::is_static_str<Msg>)
struct __init__<Exc, Msg>                                   : returns<Exc> {
    static Object unicode(const char* msg, size_t len) noexcept {
        return reinterpret_steal<Object>(
            PyUnicode_FromStringAndSize(msg, len)
        );
    }

    static Exc exception(PyObject* unicode) noexcept {
        if constexpr (std::is_invocable_v<impl::builtin_exception_map<Exc>>) {   
            return reinterpret_steal<Exc>(PyObject_CallOneArg(
                impl::builtin_exception_map<Exc>{}(),
                unicode
            ));
        } else {
            return reinterpret_steal<Exc>(PyObject_CallOneArg(
                ptr(Type<Exc>()),
                unicode
            ));
        }
    }

    template <size_t N>
    [[clang::noinline]] static auto operator()(const char(&msg)[N]) {
        Object str = unicode(msg, N - 1);
        if (str.is(nullptr)) {
            Exception::from_python();
        }
        Exc result = exception(ptr(str));
        if (result.is(nullptr)) {
            Exception::from_python();
        }

        // by default, the exception will have an empty traceback, so we need to
        // populate it with C++ frames if directed.
        if constexpr (DEBUG) {
            Object trace = reinterpret_steal<Object>(
                reinterpret_cast<PyObject*>(impl::build_traceback(
                    cpptrace::generate_trace(1)
                ))
            );
            if (!trace.is(nullptr)) {
                PyException_SetTraceback(ptr(result), ptr(trace));
            }
        }
        return result;
    }

    template <impl::is_static_str T>
    [[clang::noinline]] static auto operator()(const T& msg) {
        Object str = unicode(msg.data(), msg.size());
        if (str.is(nullptr)) {
            Exception::from_python();
        }
        Exc result = exception(ptr(str));
        if (result.is(nullptr)) {
            Exception::from_python();
        }

        // by default, the exception will have an empty traceback, so we need to
        // populate it with C++ frames if directed.
        if constexpr (DEBUG) {
            Object trace = reinterpret_steal<Object>(
                reinterpret_cast<PyObject*>(impl::build_traceback(
                    cpptrace::generate_trace(1)
                ))
            );
            if (!trace.is(nullptr)) {
                PyException_SetTraceback(ptr(result), ptr(trace));
            }
        }
        return result;
    }

    template <std::convertible_to<std::string_view> T>
        requires (!impl::string_literal<T> && !impl::is_static_str<T>)
    [[clang::noinline]] static auto operator()(const T& msg) {
        std::string_view view = msg;
        Object str = unicode(view.data(), view.size());
        if (str.is(nullptr)) {
            Exception::from_python();
        }
        Exc result = exception(ptr(str));
        if (result.is(nullptr)) {
            Exception::from_python();
        }

        // by default, the exception will have an empty traceback, so we need to
        // populate it with C++ frames if directed.
        if constexpr (DEBUG) {
            Object trace = reinterpret_steal<Object>(
                reinterpret_cast<PyObject*>(impl::build_traceback(
                    cpptrace::generate_trace(1)
                ))
            );
            if (!trace.is(nullptr)) {
                PyException_SetTraceback(ptr(result), ptr(trace));
            }
        }
        return result;
    }
};


template <std::derived_from<Exception> Exc>
struct __init__<Exc>                                        : returns<Exc> {
    static auto operator()() {
        static constexpr static_str empty = "";
        return Exc(empty);
    }
};


inline void interface<Exception>::to_python() {
    try {
        throw;
    } catch (const Exception& err) {
        PyThreadState_Get()->current_exception = Py_NewRef(ptr(err));
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


#define BUILTIN_EXCEPTION(CLS, BASE, PYTYPE, PYOBJECT)                                  \
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
    struct interface<CLS> : interface<BASE> {};                                         \
    template <>                                                                         \
    struct interface<Type<CLS>> : interface<Type<BASE>> {};                             \
                                                                                        \
    struct CLS : Exception, interface<CLS> {                                            \
        struct __python__ : def<__python__, CLS>, PYOBJECT {                            \
            static Type<CLS> __import__();                                              \
        };                                                                              \
                                                                                        \
        CLS(PyObject* p, borrowed_t t) : Exception(p, t) {}                             \
        CLS(PyObject* p, stolen_t t) : Exception(p, t) {}                               \
                                                                                        \
        template <typename T = CLS> requires (__initializer__<T>::enable)               \
        [[clang::noinline]] CLS(                                                        \
            const std::initializer_list<typename __initializer__<T>::type>& init        \
        ) : Exception(__initializer__<T>{}(init)) {}                                    \
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


BUILTIN_EXCEPTION(ArithmeticError, Exception, PyExc_ArithmeticError, PyBaseExceptionObject)
    BUILTIN_EXCEPTION(FloatingPointError, ArithmeticError, PyExc_FloatingPointError, PyBaseExceptionObject)
    BUILTIN_EXCEPTION(OverflowError, ArithmeticError, PyExc_OverflowError, PyBaseExceptionObject)
    BUILTIN_EXCEPTION(ZeroDivisionError, ArithmeticError, PyExc_ZeroDivisionError, PyBaseExceptionObject)
BUILTIN_EXCEPTION(AssertionError, Exception, PyExc_AssertionError, PyBaseExceptionObject)
BUILTIN_EXCEPTION(AttributeError, Exception, PyExc_AttributeError, PyAttributeErrorObject)
BUILTIN_EXCEPTION(BufferError, Exception, PyExc_BufferError, PyBaseExceptionObject)
BUILTIN_EXCEPTION(EOFError, Exception, PyExc_EOFError, PyBaseExceptionObject)
BUILTIN_EXCEPTION(ImportError, Exception, PyExc_ImportError, PyImportErrorObject)
    BUILTIN_EXCEPTION(ModuleNotFoundError, ImportError, PyExc_ModuleNotFoundError, PyImportErrorObject)
BUILTIN_EXCEPTION(LookupError, Exception, PyExc_LookupError, PyBaseExceptionObject)
    BUILTIN_EXCEPTION(IndexError, LookupError, PyExc_IndexError, PyBaseExceptionObject)
    BUILTIN_EXCEPTION(KeyError, LookupError, PyExc_KeyError, PyBaseExceptionObject)
BUILTIN_EXCEPTION(MemoryError, Exception, PyExc_MemoryError, PyBaseExceptionObject)
BUILTIN_EXCEPTION(NameError, Exception, PyExc_NameError, PyNameErrorObject)
    BUILTIN_EXCEPTION(UnboundLocalError, NameError, PyExc_UnboundLocalError, PyNameErrorObject)
BUILTIN_EXCEPTION(OSError, Exception, PyExc_OSError, PyOSErrorObject)
    BUILTIN_EXCEPTION(BlockingIOError, OSError, PyExc_BlockingIOError, PyOSErrorObject)
    BUILTIN_EXCEPTION(ChildProcessError, OSError, PyExc_ChildProcessError, PyOSErrorObject)
    BUILTIN_EXCEPTION(ConnectionError, OSError, PyExc_ConnectionError, PyOSErrorObject)
        BUILTIN_EXCEPTION(BrokenPipeError, ConnectionError, PyExc_BrokenPipeError, PyOSErrorObject)
        BUILTIN_EXCEPTION(ConnectionAbortedError, ConnectionError, PyExc_ConnectionAbortedError, PyOSErrorObject)
        BUILTIN_EXCEPTION(ConnectionRefusedError, ConnectionError, PyExc_ConnectionRefusedError, PyOSErrorObject)
        BUILTIN_EXCEPTION(ConnectionResetError, ConnectionError, PyExc_ConnectionResetError, PyOSErrorObject)
    BUILTIN_EXCEPTION(FileExistsError, OSError, PyExc_FileExistsError, PyOSErrorObject)
    BUILTIN_EXCEPTION(FileNotFoundError, OSError, PyExc_FileNotFoundError, PyOSErrorObject)
    BUILTIN_EXCEPTION(InterruptedError, OSError, PyExc_InterruptedError, PyOSErrorObject)
    BUILTIN_EXCEPTION(IsADirectoryError, OSError, PyExc_IsADirectoryError, PyOSErrorObject)
    BUILTIN_EXCEPTION(NotADirectoryError, OSError, PyExc_NotADirectoryError, PyOSErrorObject)
    BUILTIN_EXCEPTION(PermissionError, OSError, PyExc_PermissionError, PyOSErrorObject)
    BUILTIN_EXCEPTION(ProcessLookupError, OSError, PyExc_ProcessLookupError, PyOSErrorObject)
    BUILTIN_EXCEPTION(TimeoutError, OSError, PyExc_TimeoutError, PyOSErrorObject)
BUILTIN_EXCEPTION(ReferenceError, Exception, PyExc_ReferenceError, PyBaseExceptionObject)
BUILTIN_EXCEPTION(RuntimeError, Exception, PyExc_RuntimeError, PyBaseExceptionObject)
    BUILTIN_EXCEPTION(NotImplementedError, RuntimeError, PyExc_NotImplementedError, PyBaseExceptionObject)
    BUILTIN_EXCEPTION(RecursionError, RuntimeError, PyExc_RecursionError, PyBaseExceptionObject)
BUILTIN_EXCEPTION(StopAsyncIteration, Exception, PyExc_StopAsyncIteration, PyBaseExceptionObject)
BUILTIN_EXCEPTION(StopIteration, Exception, PyExc_StopIteration, PyStopIterationObject)
BUILTIN_EXCEPTION(SyntaxError, Exception, PyExc_SyntaxError, PySyntaxErrorObject)
    BUILTIN_EXCEPTION(IndentationError, SyntaxError, PyExc_IndentationError, PySyntaxErrorObject)
        BUILTIN_EXCEPTION(TabError, IndentationError, PyExc_TabError, PySyntaxErrorObject)
BUILTIN_EXCEPTION(SystemError, Exception, PyExc_SystemError, PyBaseExceptionObject)
BUILTIN_EXCEPTION(TypeError, Exception, PyExc_TypeError, PyBaseExceptionObject)
BUILTIN_EXCEPTION(ValueError, Exception, PyExc_ValueError, PyBaseExceptionObject)
    BUILTIN_EXCEPTION(UnicodeError, ValueError, PyExc_UnicodeError, PyUnicodeErrorObject)
        // BUILTIN_EXCEPTION(UnicodeDecodeError, UnicodeError, PyExc_UnicodeDecodeError, PyUnicodeErrorObject)
        // BUILTIN_EXCEPTION(UnicodeEncodeError, UnicodeError, PyExc_UnicodeEncodeError, PyUnicodeErrorObject)
        // BUILTIN_EXCEPTION(UnicodeTranslateError, UnicodeError, PyExc_UnicodeTranslateError, PyUnicodeErrorObject)

#undef BUILTIN_EXCEPTION


struct UnicodeDecodeError;


template <>
struct impl::builtin_exception_map<UnicodeDecodeError> {
    [[nodiscard]] static PyObject* operator()() {
        return PyExc_UnicodeDecodeError;
    }
};


template <>
struct interface<UnicodeDecodeError> : interface<UnicodeError> {
    __declspec(property(get=_encoding)) std::string encoding;
    [[nodiscard]] std::string _encoding(this const auto& self);

    __declspec(property(get=_object)) std::string object;
    [[nodiscard]] std::string _object(this const auto& self);

    __declspec(property(get=_start)) Py_ssize_t start;
    [[nodiscard]] Py_ssize_t _start(this const auto& self);

    __declspec(property(get=_end)) Py_ssize_t end;
    [[nodiscard]] Py_ssize_t _end(this const auto& self);

    __declspec(property(get=_reason)) std::string reason;
    [[nodiscard]] std::string _reason(this const auto& self);
};


struct UnicodeDecodeError : Exception, interface<UnicodeDecodeError> {
    struct __python__ : def<__python__, UnicodeDecodeError>, PyUnicodeErrorObject {
        static Type<UnicodeDecodeError> __import__();
    };

    UnicodeDecodeError(PyObject* p, borrowed_t t) : Exception(p, t) {}
    UnicodeDecodeError(PyObject* p, stolen_t t) : Exception(p, t) {}

    template <typename T = UnicodeDecodeError> requires (__initializer__<T>::enable)
    [[clang::noinline]] UnicodeDecodeError(
        const std::initializer_list<typename __initializer__<T>::type>& init
    ) : Object(__initializer__<T>{}(init)) {}

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
        return m_message.value().data();
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
        return m_what.value().data();
    }

};


template <>
struct interface<Type<UnicodeDecodeError>> : interface<Type<UnicodeError>> {
    [[nodiscard]] static std::string encoding(const auto& self) {
        return self.encoding;
    }
    [[nodiscard]] static std::string object(const auto& self) {
        return self.object;
    }
    [[nodiscard]] static Py_ssize_t start(const auto& self) {
        return self.start;
    }
    [[nodiscard]] static Py_ssize_t end(const auto& self) {
        return self.end;
    }
    [[nodiscard]] static std::string reason(const auto& self) {
        return self.reason;
    }
};


template <>
struct __init__<UnicodeDecodeError>                         : disable {};
template <typename Msg>
    requires (std::convertible_to<Msg, std::string_view> || impl::is_static_str<Msg>)
struct __init__<UnicodeDecodeError, Msg>                    : disable {};


template <
    std::convertible_to<std::string> Encoding,
    std::convertible_to<std::string> Obj,
    std::convertible_to<Py_ssize_t> Start,
    std::convertible_to<Py_ssize_t> End,
    std::convertible_to<std::string> Reason
>
struct __init__<UnicodeDecodeError, Encoding, Obj, Start, End, Reason> :
    returns<UnicodeDecodeError>
{
    [[clang::noinline]] static auto operator()(
        const std::string& encoding,
        const std::string& object,
        Py_ssize_t start,
        Py_ssize_t end,
        const std::string& reason
    ) {
        PyObject* result = PyUnicodeDecodeError_Create(
            encoding.data(),
            object.data(),
            object.size(),
            start,
            end,
            reason.data()
        );
        if (result == nullptr) {
            Exception::from_python();
        }

        if constexpr (DEBUG) {
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
        }

        return reinterpret_steal<UnicodeDecodeError>(result);
    }
};


[[nodiscard]] inline std::string interface<UnicodeDecodeError>::_encoding(
    this const auto& self
) {
    PyObject* encoding = PyUnicodeDecodeError_GetEncoding(ptr(self));
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


[[nodiscard]] inline std::string interface<UnicodeDecodeError>::_object(
    this const auto& self
) {
    PyObject* object = PyUnicodeDecodeError_GetObject(ptr(self));
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


[[nodiscard]] inline Py_ssize_t interface<UnicodeDecodeError>::_start(
    this const auto& self
) {
    Py_ssize_t start;
    if (PyUnicodeDecodeError_GetStart(ptr(self), &start)) {
        Exception::from_python();
    }
    return start;
}


[[nodiscard]] inline Py_ssize_t interface<UnicodeDecodeError>::_end(
    this const auto& self
) {
    Py_ssize_t end;
    if (PyUnicodeDecodeError_GetEnd(ptr(self), &end)) {
        Exception::from_python();
    }
    return end;
}


[[nodiscard]] inline std::string interface<UnicodeDecodeError>::_reason(
    this const auto& self
) {
    PyObject* reason = PyUnicodeDecodeError_GetReason(ptr(self));
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
struct interface<UnicodeEncodeError> : interface<UnicodeError> {
    __declspec(property(get=_encoding)) std::string encoding;
    [[nodiscard]] std::string _encoding(this const auto& self);

    __declspec(property(get=_object)) std::string object;
    [[nodiscard]] std::string _object(this const auto& self);

    __declspec(property(get=_start)) Py_ssize_t start;
    [[nodiscard]] Py_ssize_t _start(this const auto& self);

    __declspec(property(get=_end)) Py_ssize_t end;
    [[nodiscard]] Py_ssize_t _end(this const auto& self);

    __declspec(property(get=_reason)) std::string reason;
    [[nodiscard]] std::string _reason(this const auto& self);
};


struct UnicodeEncodeError : Exception, interface<UnicodeEncodeError> {
    struct __python__ : def<__python__, UnicodeEncodeError>, PyUnicodeErrorObject {
        static Type<UnicodeEncodeError> __import__();
    };

    UnicodeEncodeError(PyObject* p, borrowed_t t) : Exception(p, t) {}
    UnicodeEncodeError(PyObject* p, stolen_t t) : Exception(p, t) {}

    template <typename T = UnicodeEncodeError> requires (__initializer__<T>::enable)
    [[clang::noinline]] UnicodeEncodeError(
        const std::initializer_list<typename __initializer__<T>::type>& init
    ) : Object(__initializer__<T>{}(init)) {}

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
        return m_message.value().data();
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
        return m_what.value().data();
    }

};


template <>
struct interface<Type<UnicodeEncodeError>> : interface<Type<UnicodeError>> {
    [[nodiscard]] static std::string encoding(const auto& self) {
        return self.encoding;
    }
    [[nodiscard]] static std::string object(const auto& self) {
        return self.object;
    }
    [[nodiscard]] static Py_ssize_t start(const auto& self) {
        return self.start;
    }
    [[nodiscard]] static Py_ssize_t end(const auto& self) {
        return self.end;
    }
    [[nodiscard]] static std::string reason(const auto& self) {
        return self.reason;
    }
};


template <>
struct __init__<UnicodeEncodeError>                         : disable {};
template <typename Msg>
    requires (std::convertible_to<Msg, std::string_view> || impl::is_static_str<Msg>)
struct __init__<UnicodeEncodeError, Msg>                    : disable {};


template <
    std::convertible_to<std::string> Encoding,
    std::convertible_to<std::string> Obj,
    std::convertible_to<Py_ssize_t> Start,
    std::convertible_to<Py_ssize_t> End,
    std::convertible_to<std::string> Reason
>
struct __init__<UnicodeEncodeError, Encoding, Obj, Start, End, Reason> :
    returns<UnicodeEncodeError>
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
            encoding.data(),
            object.data(),
            start,
            end,
            reason.data()
        );
        if (result == nullptr) {
            Exception::from_python();
        }

        if constexpr (DEBUG) {
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
        }

        return reinterpret_steal<UnicodeEncodeError>(result);
    }
};


[[nodiscard]] inline std::string interface<UnicodeEncodeError>::_encoding(
    this const auto& self
) {
    PyObject* encoding = PyUnicodeEncodeError_GetEncoding(ptr(self));
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


[[nodiscard]] inline std::string interface<UnicodeEncodeError>::_object(
    this const auto& self
) {
    PyObject* object = PyUnicodeEncodeError_GetObject(ptr(self));
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


[[nodiscard]] inline Py_ssize_t interface<UnicodeEncodeError>::_start(
    this const auto& self
) {
    Py_ssize_t start;
    if (PyUnicodeEncodeError_GetStart(ptr(self), &start)) {
        Exception::from_python();
    }
    return start;
}


[[nodiscard]] inline Py_ssize_t interface<UnicodeEncodeError>::_end(
    this const auto& self
) {
    Py_ssize_t end;
    if (PyUnicodeEncodeError_GetEnd(ptr(self), &end)) {
        Exception::from_python();
    }
    return end;
}


[[nodiscard]] inline std::string interface<UnicodeEncodeError>::_reason(
    this const auto& self
) {
    PyObject* reason = PyUnicodeEncodeError_GetReason(ptr(self));
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
struct interface<UnicodeTranslateError> : interface<UnicodeError> {
    __declspec(property(get=_object)) std::string object;
    [[nodiscard]] std::string _object(this const auto& self);

    __declspec(property(get=_start)) Py_ssize_t start;
    [[nodiscard]] Py_ssize_t _start(this const auto& self);

    __declspec(property(get=_end)) Py_ssize_t end;
    [[nodiscard]] Py_ssize_t _end(this const auto& self);

    __declspec(property(get=_reason)) std::string reason;
    [[nodiscard]] std::string _reason(this const auto& self);
};


struct UnicodeTranslateError : Exception, interface<UnicodeTranslateError> {
    struct __python__ : def<__python__, UnicodeTranslateError>, PyUnicodeErrorObject {
        static Type<UnicodeTranslateError> __import__();
    };

    UnicodeTranslateError(PyObject* p, borrowed_t t) : Exception(p, t) {}
    UnicodeTranslateError(PyObject* p, stolen_t t) : Exception(p, t) {}

    template <typename T = UnicodeTranslateError> requires (__initializer__<T>::enable)
    [[clang::noinline]] UnicodeTranslateError(
        const std::initializer_list<typename __initializer__<T>::type>& init
    ) : Object(__initializer__<T>{}(init)) {}

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
        return m_message.value().data();
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
        return m_what.value().data();
    }

};


template <>
struct interface<Type<UnicodeTranslateError>> : interface<Type<UnicodeError>> {
    [[nodiscard]] static std::string object(const auto& self) {
        return self.object;
    }
    [[nodiscard]] static Py_ssize_t start(const auto& self) {
        return self.start;
    }
    [[nodiscard]] static Py_ssize_t end(const auto& self) {
        return self.end;
    }
    [[nodiscard]] static std::string reason(const auto& self) {
        return self.reason;
    }
};


template <>
struct __init__<UnicodeTranslateError>                      : disable {};
template <typename Msg>
    requires (std::convertible_to<Msg, std::string_view> || impl::is_static_str<Msg>)
struct __init__<UnicodeTranslateError, Msg>                 : disable {};


template <
    std::convertible_to<std::string> Encoding,
    std::convertible_to<std::string> Obj,
    std::convertible_to<Py_ssize_t> Start,
    std::convertible_to<Py_ssize_t> End,
    std::convertible_to<std::string> Reason
>
struct __init__<UnicodeTranslateError, Encoding, Obj, Start, End, Reason> :
    returns<UnicodeTranslateError>
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
            object.data(),
            start,
            end,
            reason.data()
        );
        if (result == nullptr) {
            Exception::from_python();
        }

        if constexpr (DEBUG) {
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
        }

        return reinterpret_steal<UnicodeTranslateError>(result);
    }
};


[[nodiscard]] inline std::string interface<UnicodeTranslateError>::_object(
    this const auto& self
) {
    PyObject* object = PyUnicodeTranslateError_GetObject(ptr(self));
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


[[nodiscard]] inline Py_ssize_t interface<UnicodeTranslateError>::_start(
    this const auto& self
) {
    Py_ssize_t start;
    if (PyUnicodeTranslateError_GetStart(ptr(self), &start)) {
        Exception::from_python();
    }
    return start;
}


[[nodiscard]] inline Py_ssize_t interface<UnicodeTranslateError>::_end(
    this const auto& self
) {
    Py_ssize_t end;
    if (PyUnicodeTranslateError_GetEnd(ptr(self), &end)) {
        Exception::from_python();
    }
    return end;
}


[[nodiscard]] inline std::string interface<UnicodeTranslateError>::_reason(
    this const auto& self
) {
    PyObject* reason = PyUnicodeTranslateError_GetReason(ptr(self));
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


/////////////////////////
////    OPERATORS    ////
/////////////////////////


template <size_t N>
[[gnu::always_inline]] inline void assert_(bool condition, const char(&message)[N]) {
    if constexpr (DEBUG) {
        if (!condition) {
            throw AssertionError(message);
        }
    }
}


template <impl::is_static_str T>
[[gnu::always_inline]] inline void assert_(bool condition, const T& message) {
    if constexpr (DEBUG) {
        if (!condition) {
            throw AssertionError(message);
        }
    }
}


template <std::convertible_to<std::string_view> T>
    requires (!impl::string_literal<T> && !impl::is_static_str<T>)
[[gnu::always_inline]] inline void assert_(bool condition, const T& message) {
    if constexpr (DEBUG) {
        if (!condition) {
            throw AssertionError(message);
        }
    }
}


}  // namespace py


#endif
