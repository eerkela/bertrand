#ifndef BERTRAND_PYTHON_CORE_EXCEPT_H
#define BERTRAND_PYTHON_CORE_EXCEPT_H

#include "declarations.h"
#include "object.h"
#include "pyport.h"
#include "traceback.h"
#include <cpptrace/cpptrace.hpp>


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
    static auto operator()() {
        PyFrameObject* frame = PyEval_GetFrame();
        if (frame == nullptr) {
            throw RuntimeError("no frame is currently executing");  // TODO: forward declaration
        }
        return reinterpret_borrow<Frame>(reinterpret_cast<PyObject*>(frame));
    }
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
    static Frame operator()(int skip) {
        PyFrameObject* frame = reinterpret_cast<PyFrameObject*>(
            Py_XNewRef(PyEval_GetFrame())
        );
        if (frame == nullptr) {
            throw RuntimeError("no frame is currently executing");  // TODO: forward declaration
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
};


/* Execute the bytecode object stored within a Python frame using its current context.
This is the main entry point for the Python interpreter, and causes the program to run
until it either terminates or encounters an error.  The return value is the result of
the last evaluated expression, which can be the return value of a function, the yield
value of a generator, etc. */
template <>
struct __call__<Frame> : Returns<Object> {
    static auto operator()(const Frame& frame) {
        PyObject* result = PyEval_EvalFrame(
            reinterpret_cast<PyFrameObject*>(ptr(frame))
        );
        if (result == nullptr) {
            Exception::from_python();  // TODO: forward declaration
        }
        return reinterpret_steal<Object>(result);
    }
};


[[nodiscard]] bool Interface<Frame>::has_code() const {
    return PyFrame_GetCode(
        reinterpret_cast<PyFrameObject*>(
            ptr(reinterpret_cast<const Object&>(*this))
        )
    ) != nullptr;
}


[[nodiscard]] Frame Interface<Frame>::back() const {
    PyFrameObject* result = PyFrame_GetBack(
        reinterpret_cast<PyFrameObject*>(
            ptr(reinterpret_cast<const Object&>(*this))
        )
    );
    if (result == nullptr) {
        Exception::from_python();  // TODO: forward declaration
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
            Exception::from_python();  // TODO: forward declaration
        }
        out += "File \"" + std::string(name, len) + "\", line ";
        out += std::to_string(PyFrame_GetLineNumber(frame)) + ", in ";
        name = PyUnicode_AsUTF8AndSize(code->co_name, &len);
        if (name == nullptr) {
            Py_DECREF(code);
            Exception::from_python();  // TODO: forward declaration
        }
        out += std::string(name, len);
        Py_DECREF(code);
    } else {
        out += "File \"<unknown>\", line 0, in <unknown>";
    }

    return out;
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

                // ignore frames that originate from the environment, and are not part
                // of the user's code
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

    [[nodiscard]] value_type operator*() const {
        if (curr == nullptr) {
            throw StopIteration();  // TODO: forward declaration
        }
        return reinterpret_borrow<Frame>(
            reinterpret_cast<PyObject*>(curr->tb_frame)
        );
    }

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

    [[nodiscard]] value_type operator*() const {
        if (index < 0) {
            throw StopIteration();  // TODO: forward declaration
        }
        return reinterpret_borrow<Frame>(
            reinterpret_cast<PyObject*>(frames[index]->tb_frame)
        );
    }

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


struct Exception;


template <>
struct Interface<Exception> {

};


struct Exception : std::exception, Object, Interface<Exception> {

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

};


//////////////////////////
////    EXTENSIONS    ////
//////////////////////////





namespace impl {

    /* A language-agnostic stack frame that is used when reporting mixed Python/C++
    error tracebacks. */
    struct StackFrame : BertrandTag {
    private:
        PyThreadState* thread = PyThreadState_Get();
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
            bool is_inline = false
        ) : filename(filename),
            funcname(parse_function_name(funcname)),
            lineno(lineno),
            is_inline(is_inline)
        {}

        StackFrame(const cpptrace::stacktrace_frame& frame) :
            filename(frame.filename),
            funcname(parse_function_name(frame.symbol)),
            lineno(frame.line.value_or(0)),
            is_inline(frame.is_inline)
        {}

        StackFrame(PyFrameObject* frame = nullptr) :
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
        PyThreadState* thread = PyThreadState_Get();

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

        StackTrace(const std::deque<StackFrame>& stack) : stack(stack) {}
        StackTrace(std::deque<StackFrame>&& stack) : stack(std::move(stack)) {}

        explicit StackTrace(const cpptrace::stacktrace& stacktrace) {
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
            const cpptrace::stacktrace& stacktrace
        ) {
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


/* Root exception class.  Appends a C++ stack trace that will be propagated up to
Python, and allows translation of arbitrary exceptions across the language boundary. */
struct Exception : public std::exception, impl::BertrandTag {
private:

    static std::string exc_string(PyObject* obj) {
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

protected:
    std::string what_msg;
    mutable std::string what_cache;

public:

    #ifdef BERTRAND_NO_TRACEBACK

        explicit Exception(std::string&& message = "", size_t skip = 0) :
            what_msg(std::move(message))
        {}

        explicit Exception(std::string&& message, const cpptrace::stacktrace& trace) :
            what_msg(std::move(message))
        {}

        explicit Exception(const std::string& message, size_t skip = 0) :
            what_msg(message)
        {}

        explicit Exception(const std::string& message, const cpptrace::stacktrace& trace) :
            what_msg(message)
        {}

        explicit Exception(
            PyObject* type,
            PyObject* value,
            PyObject* traceback,
            size_t skip = 0
        ) : what_msg(value == nullptr ? std::string() : exc_string(value))
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
            const cpptrace::stacktrace& trace
        ) : what_msg(value == nullptr ? std::string() : exc_string(value))
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
            std::string&& message = "",
            size_t skip = 0
        ) : what_msg((Interpreter::init(), std::move(message))),
            traceback(cpptrace::generate_trace(++skip))
        {}

        [[clang::noinline]] explicit Exception(
            const std::string& message,
            size_t skip = 0
        ) : what_msg((Interpreter::init(), message)),
            traceback(cpptrace::generate_trace(++skip))
        {}

        [[clang::noinline]] explicit Exception(
            std::string&& message,
            const cpptrace::stacktrace& trace
        ) : what_msg((Interpreter::init(), std::move(message))),
            traceback(trace)
        {}

        [[clang::noinline]] explicit Exception(
            const std::string& message,
            const cpptrace::stacktrace& trace
        ) : what_msg((Interpreter::init(), message)),
            traceback(trace)
        {}

        [[clang::noinline]] explicit Exception(
            PyObject* type,
            PyObject* value,
            PyObject* traceback,
            size_t skip = 0
        ) : what_msg(value == nullptr ? std::string() : exc_string(value)),
            traceback(
                reinterpret_cast<PyTracebackObject*>(traceback),
                cpptrace::generate_trace(++skip)
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
            const cpptrace::stacktrace& trace
        ) : what_msg(value == nullptr ? std::string() : exc_string(value)),
            traceback(
                reinterpret_cast<PyTracebackObject*>(traceback),
                trace
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
    [[noreturn, clang::noinline]] static void from_python(size_t skip = 0);

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
            std::string&& message = "",
            size_t skip = 0
        ) : Base(std::move(message), cpptrace::generate_trace(++skip))
        {}

        [[clang::noinline]] explicit __exception__(
            const std::string& message,
            size_t skip = 0
        ) : Base(message, cpptrace::generate_trace(++skip))
        {}

        [[clang::noinline]] explicit __exception__(
            std::string&& message,
            const cpptrace::stacktrace& trace
        ) : Base(std::move(message), trace)
        {}

        [[clang::noinline]] explicit __exception__(
            const std::string& message,
            const cpptrace::stacktrace& trace
        ) : Base(message, trace)
        {}

        [[clang::noinline]] explicit __exception__(
            PyObject* type,
            PyObject* value,
            PyObject* traceback,
            size_t skip = 0
        ) : Base(type, value, traceback, cpptrace::generate_trace(++skip))
        {}

        [[clang::noinline]] explicit __exception__(
            PyObject* type,
            PyObject* value,
            PyObject* traceback,
            const cpptrace::stacktrace& trace
        ) : Base(type, value, traceback, trace)
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

    static void from_python(size_t skip = 0) = delete;

};


/* CPython exception types:
 *      https://docs.python.org/3/c-api/exceptions.html#standard-exceptions
 *
 * Inheritance hierarchy:
 *      https://docs.python.org/3/library/exceptions.html#exception-hierarchy
 */


#ifdef BERTRAND_NO_TRACEBACK
    #define BUILTIN_EXCEPTION(CLS, BASE, PYTYPE) \
        struct CLS : __exception__<CLS, BASE> { \
            void set_pyerr() const override { \
                PyErr_SetString(PYTYPE, message().c_str()); \
            } \
        };
#else
    #define BUILTIN_EXCEPTION(CLS, BASE, PYTYPE) \
        struct CLS : __exception__<CLS, BASE> { \
            using __exception__::__exception__; \
            void set_pyerr() const override { \
                traceback.restore(PYTYPE, message().c_str()); \
            } \
        };
#endif


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
        // BUILTIN_EXCEPTION(UnicodeDecodeError, UnicodeError, PyExc_UnicodeDecodeError)
        // BUILTIN_EXCEPTION(UnicodeEncodeError, UnicodeError, PyExc_UnicodeEncodeError)
        // BUILTIN_EXCEPTION(UnicodeTranslateError, UnicodeError, PyExc_UnicodeTranslateError)


struct UnicodeDecodeError : __exception__<UnicodeDecodeError, UnicodeError> {
    using Base = __exception__<UnicodeDecodeError, UnicodeError>;

protected:

    static std::string generate_message(
        const std::string& encoding,
        const std::string& object,
        Py_ssize_t start,
        Py_ssize_t end,
        const std::string& reason
    ) {
        return (
            "'" + encoding + "' codec can't decode bytes in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) + ": " +
            reason
        );
    }

public:
    std::string encoding;
    std::string object;
    Py_ssize_t start;
    Py_ssize_t end;
    std::string reason;

    [[clang::noinline]] explicit UnicodeDecodeError(
        const std::string& encoding,
        const std::string& object,
        Py_ssize_t start,
        Py_ssize_t end,
        const std::string& reason,
        size_t skip = 0
    ) : UnicodeDecodeError(
            encoding,
            object,
            start,
            end,
            reason,
            cpptrace::generate_trace(skip + 2)
        )
    {}

    [[clang::noinline]] explicit UnicodeDecodeError(
        const std::string& encoding,
        const std::string& object,
        Py_ssize_t start,
        Py_ssize_t end,
        const std::string& reason,
        const cpptrace::stacktrace& trace
    ) : Base(generate_message(encoding, object, start, end, reason), trace),
        encoding(encoding),
        object(object),
        start(start),
        end(end),
        reason(reason)
    {}

    [[clang::noinline]] explicit UnicodeDecodeError(
        PyObject* type,
        PyObject* value,
        PyObject* traceback,
        size_t skip = 0
    ) : UnicodeDecodeError(
        type,
        value,
        traceback,
        cpptrace::generate_trace(skip + 2)
    ) {}

    [[clang::noinline]] explicit UnicodeDecodeError(
        PyObject* type,
        PyObject* value,
        PyObject* traceback,
        const cpptrace::stacktrace& trace
    ) : Base(type, value, traceback, trace),
        encoding([value] {
            PyObject* encoding = PyUnicodeDecodeError_GetEncoding(value);
            if (encoding == nullptr) {
                throw std::runtime_error(
                    "could not extract encoding from UnicodeDecodeError"
                );
            }
            Py_ssize_t size;
            const char* data = PyUnicode_AsUTF8AndSize(encoding, &size);
            if (data == nullptr) {
                Py_DECREF(encoding);
                throw std::runtime_error(
                    "could not extract encoding from UnicodeDecodeError"
                );
            }
            std::string result(data, size);
            Py_DECREF(encoding);
            return result;
        }()),
        object([value] {
            PyObject* object = PyUnicodeDecodeError_GetObject(value);
            if (object == nullptr) {
                throw std::runtime_error(
                    "could not extract object from UnicodeDecodeError"
                );
            }
            Py_ssize_t size;
            const char* data = PyUnicode_AsUTF8AndSize(object, &size);
            if (data == nullptr) {
                Py_DECREF(object);
                throw std::runtime_error(
                    "could not extract object from UnicodeDecodeError"
                );
            }
            std::string result(data, size);
            Py_DECREF(object);
            return result;
        }()),
        start([value] {
            Py_ssize_t start;
            if (PyUnicodeDecodeError_GetStart(value, &start)) {
                throw std::runtime_error(
                    "could not extract start from UnicodeDecodeError"
                );
            }
            return start;
        }()),
        end([value] {
            Py_ssize_t end;
            if (PyUnicodeDecodeError_GetEnd(value, &end)) {
                throw std::runtime_error(
                    "could not extract end from UnicodeDecodeError"
                );
            }
            return end;
        }()),
        reason([value] {
            PyObject* reason = PyUnicodeDecodeError_GetReason(value);
            if (reason == nullptr) {
                throw std::runtime_error(
                    "could not extract reason from UnicodeDecodeError"
                );
            }
            Py_ssize_t size;
            const char* data = PyUnicode_AsUTF8AndSize(reason, &size);
            if (data == nullptr) {
                Py_DECREF(reason);
                throw std::runtime_error(
                    "could not extract reason from UnicodeDecodeError"
                );
            }
            std::string result(data, size);
            Py_DECREF(reason);
            return result;
        }())
    {}

    const char* what() const noexcept override {
        if (what_cache.empty()) {
            #ifndef BERTRAND_NO_TRACEBACK
                what_cache += traceback.to_string() + "\n";
            #endif
            what_cache += "UnicodeDecodeError: " + message();
        }
        return what_cache.c_str();
    }

    void set_pyerr() const override {
        PyErr_Clear();
        PyObject* exc = PyUnicodeDecodeError_Create(
            encoding.c_str(),
            object.c_str(),
            object.size(),
            start,
            end,
            reason.c_str()
        );
        if (exc == nullptr) {
            PyErr_SetString(PyExc_UnicodeError, message().c_str());
        } else {
            PyTracebackObject* tb = traceback.to_python();
            if (tb != nullptr) {
                PyException_SetTraceback(exc, reinterpret_cast<PyObject*>(tb));
            }
            PyThreadState* thread = PyThreadState_Get();
            thread->current_exception = exc;
        }
    }

};


struct UnicodeEncodeError : __exception__<UnicodeEncodeError, UnicodeError> {
    using Base = __exception__<UnicodeEncodeError, UnicodeError>;

protected:

    static std::string generate_message(
        const std::string& encoding,
        const std::string& object,
        Py_ssize_t start,
        Py_ssize_t end,
        const std::string& reason
    ) {
        return (
            "'" + encoding + "' codec can't encode characters in position " +
            std::to_string(start) + "-" + std::to_string(end - 1) + ": " +
            reason
        );
    }

public:
    std::string encoding;
    std::string object;
    Py_ssize_t start = 0;
    Py_ssize_t end = 0;
    std::string reason;

    [[clang::noinline]] explicit UnicodeEncodeError(
        const std::string& encoding,
        const std::string& object,
        Py_ssize_t start,
        Py_ssize_t end,
        const std::string& reason,
        size_t skip = 0
    ) : UnicodeEncodeError(
            encoding,
            object,
            start,
            end,
            reason,
            cpptrace::generate_trace(skip + 2)
        )
    {}

    [[clang::noinline]] explicit UnicodeEncodeError(
        const std::string& encoding,
        const std::string& object,
        Py_ssize_t start,
        Py_ssize_t end,
        const std::string& reason,
        const cpptrace::stacktrace& trace
    ) : Base(generate_message(encoding, object, start, end, reason), trace),
        encoding(encoding),
        object(object),
        start(start),
        end(end),
        reason(reason)
    {}

    [[clang::noinline]] explicit UnicodeEncodeError(
        PyObject* type,
        PyObject* value,
        PyObject* traceback,
        size_t skip = 0
    ) : UnicodeEncodeError(
        type,
        value,
        traceback,
        cpptrace::generate_trace(skip + 2)
    ) {}

    [[clang::noinline]] explicit UnicodeEncodeError(
        PyObject* type,
        PyObject* value,
        PyObject* traceback,
        const cpptrace::stacktrace& trace
    ) : Base(type, value, traceback, trace),
        encoding([value] {
            PyObject* encoding = PyUnicodeEncodeError_GetEncoding(value);
            if (encoding == nullptr) {
                throw std::runtime_error(
                    "could not extract encoding from UnicodeEncodeError"
                );
            }
            Py_ssize_t size;
            const char* data = PyUnicode_AsUTF8AndSize(encoding, &size);
            if (data == nullptr) {
                Py_DECREF(encoding);
                throw std::runtime_error(
                    "could not extract encoding from UnicodeEncodeError"
                );
            }
            std::string result(data, size);
            Py_DECREF(encoding);
            return result;
        }()),
        object([value] {
            PyObject* object = PyUnicodeEncodeError_GetObject(value);
            if (object == nullptr) {
                throw std::runtime_error(
                    "could not extract object from UnicodeEncodeError"
                );
            }
            Py_ssize_t size;
            const char* data = PyUnicode_AsUTF8AndSize(object, &size);
            if (data == nullptr) {
                Py_DECREF(object);
                throw std::runtime_error(
                    "could not extract object from UnicodeEncodeError"
                );
            }
            std::string result(data, size);
            Py_DECREF(object);
            return result;
        }()),
        start([value] {
            Py_ssize_t start;
            if (PyUnicodeEncodeError_GetStart(value, &start)) {
                throw std::runtime_error(
                    "could not extract start from UnicodeEncodeError"
                );
            }
            return start;
        }()),
        end([value] {
            Py_ssize_t end;
            if (PyUnicodeEncodeError_GetEnd(value, &end)) {
                throw std::runtime_error(
                    "could not extract end from UnicodeEncodeError"
                );
            }
            return end;
        }()),
        reason([value] {
            PyObject* reason = PyUnicodeEncodeError_GetReason(value);
            if (reason == nullptr) {
                throw std::runtime_error(
                    "could not extract reason from UnicodeEncodeError"
                );
            }
            Py_ssize_t size;
            const char* data = PyUnicode_AsUTF8AndSize(reason, &size);
            if (data == nullptr) {
                Py_DECREF(reason);
                throw std::runtime_error(
                    "could not extract reason from UnicodeEncodeError"
                );
            }
            std::string result(data, size);
            Py_DECREF(reason);
            return result;
        }())
    {}

    const char* what() const noexcept override {
        if (what_cache.empty()) {
            #ifndef BERTRAND_NO_TRACEBACK
                what_cache += traceback.to_string() + "\n";
            #endif
            what_cache += "UnicodeEncodeError: " + message();
        }
        return what_cache.c_str();
    }

    void set_pyerr() const override {
        PyErr_Clear();
        PyObject* exc = PyObject_CallFunction(
            PyExc_UnicodeEncodeError,
            "ssnns",
            encoding.c_str(),
            object.c_str(),
            start,
            end,
            reason.c_str()
        );
        if (exc == nullptr) {
            PyErr_SetString(PyExc_UnicodeError, message().c_str());
        } else {
            PyTracebackObject* tb = traceback.to_python();
            if (tb != nullptr) {
                PyException_SetTraceback(exc, reinterpret_cast<PyObject*>(tb));
            }
            PyThreadState* thread = PyThreadState_Get();
            thread->current_exception = exc;
        }
    }

};


struct UnicodeTranslateError : __exception__<UnicodeTranslateError, UnicodeError> {
    using Base = __exception__<UnicodeTranslateError, UnicodeError>;

protected:

    static std::string generate_message(
        const std::string& object,
        Py_ssize_t start,
        Py_ssize_t end,
        const std::string& reason
    ) {
        return (
            "can't translate characters in position " + std::to_string(start) +
            "-" + std::to_string(end - 1) + ": " + reason
        );
    }

public:
    std::string object;
    Py_ssize_t start = 0;
    Py_ssize_t end = 0;
    std::string reason;

    [[clang::noinline]] explicit UnicodeTranslateError(
        const std::string& object,
        Py_ssize_t start,
        Py_ssize_t end,
        const std::string& reason,
        size_t skip = 0
    ) : UnicodeTranslateError(
            object,
            start,
            end,
            reason,
            cpptrace::generate_trace(skip + 2)
        )
    {}

    [[clang::noinline]] explicit UnicodeTranslateError(
        const std::string& object,
        Py_ssize_t start,
        Py_ssize_t end,
        const std::string& reason,
        const cpptrace::stacktrace& trace
    ) : Base(generate_message(object, start, end, reason), trace),
        object(object),
        start(start),
        end(end),
        reason(reason)
    {}

    [[clang::noinline]] explicit UnicodeTranslateError(
        PyObject* type,
        PyObject* value,
        PyObject* traceback,
        size_t skip = 0
    ) : UnicodeTranslateError(
        type,
        value,
        traceback,
        cpptrace::generate_trace(skip + 2)
    ) {}

    [[clang::noinline]] explicit UnicodeTranslateError(
        PyObject* type,
        PyObject* value,
        PyObject* traceback,
        const cpptrace::stacktrace& trace
    ) : Base(type, value, traceback, trace),
        object([value] {
            PyObject* object = PyUnicodeTranslateError_GetObject(value);
            if (object == nullptr) {
                throw std::runtime_error(
                    "could not extract object from UnicodeTranslateError"
                );
            }
            Py_ssize_t size;
            const char* data = PyUnicode_AsUTF8AndSize(object, &size);
            if (data == nullptr) {
                Py_DECREF(object);
                throw std::runtime_error(
                    "could not extract object from UnicodeTranslateError"
                );
            }
            std::string result(data, size);
            Py_DECREF(object);
            return result;
        }()),
        start([value] {
            Py_ssize_t start;
            if (PyUnicodeTranslateError_GetStart(value, &start)) {
                throw std::runtime_error(
                    "could not extract start from UnicodeTranslateError"
                );
            }
            return start;
        }()),
        end([value] {
            Py_ssize_t end;
            if (PyUnicodeTranslateError_GetEnd(value, &end)) {
                throw std::runtime_error(
                    "could not extract end from UnicodeTranslateError"
                );
            }
            return end;
        }()),
        reason([value] {
            PyObject* reason = PyUnicodeTranslateError_GetReason(value);
            if (reason == nullptr) {
                throw std::runtime_error(
                    "could not extract reason from UnicodeTranslateError"
                );
            }
            Py_ssize_t size;
            const char* data = PyUnicode_AsUTF8AndSize(reason, &size);
            if (data == nullptr) {
                Py_DECREF(reason);
                throw std::runtime_error(
                    "could not extract reason from UnicodeTranslateError"
                );
            }
            std::string result(data, size);
            Py_DECREF(reason);
            return result;
        }())
    {}

    const char* what() const noexcept override {
        if (what_cache.empty()) {
            #ifndef BERTRAND_NO_TRACEBACK
                what_cache += traceback.to_string() + "\n";
            #endif
            what_cache += "UnicodeTranslateError: " + message();
        }
        return what_cache.c_str();
    }

    void set_pyerr() const override {
        PyErr_Clear();
        PyObject* exc = PyObject_CallFunction(
            PyExc_UnicodeTranslateError,
            "snns",
            object.c_str(),
            start,
            end,
            reason.c_str()
        );
        if (exc == nullptr) {
            PyErr_SetString(PyExc_UnicodeError, message().c_str());
        } else {
            PyTracebackObject* tb = traceback.to_python();
            if (tb != nullptr) {
                PyException_SetTraceback(exc, reinterpret_cast<PyObject*>(tb));
            }
            PyThreadState* thread = PyThreadState_Get();
            thread->current_exception = exc;
        }
    }

};


#undef BUILTIN_EXCEPTION


}  // namespace py


#endif
