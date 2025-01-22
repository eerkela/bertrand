#ifndef BERTRAND_PYTHON_CORE_CODE_H
#define BERTRAND_PYTHON_CORE_CODE_H

#include "declarations.h"
#include "object.h"


namespace bertrand {


///////////////////
////   CODE    ////
///////////////////


struct Code;


/// TODO: the code interface (along with most other object interfaces) probably needs
/// to be defined super late into the standard library creation.


template <>
struct interface<Code> {
    [[nodiscard]] static Code compile(const std::string& source);

    __declspec(property(get = _get_line_number)) Py_ssize_t line_number;
    [[nodiscard]] Py_ssize_t _get_line_number(this auto&& self) noexcept;
    __declspec(property(get = _get_argcount)) Py_ssize_t argcount;
    [[nodiscard]] Py_ssize_t _get_argcount(this auto&& self) noexcept;
    __declspec(property(get = _get_posonlyargcount)) Py_ssize_t posonlyargcount;
    [[nodiscard]] Py_ssize_t _get_posonlyargcount(this auto&& self) noexcept;
    __declspec(property(get = _get_kwonlyargcount)) Py_ssize_t kwonlyargcount;
    [[nodiscard]] Py_ssize_t _get_kwonlyargcount(this auto&& self) noexcept;
    __declspec(property(get = _get_nlocals)) Py_ssize_t nlocals;
    [[nodiscard]] Py_ssize_t _get_nlocals(this auto&& self) noexcept;
    __declspec(property(get = _get_stacksize)) Py_ssize_t stacksize;
    [[nodiscard]] Py_ssize_t _get_stacksize(this auto&& self) noexcept;
    __declspec(property(get = _get_flags)) int flags;
    [[nodiscard]] int _get_flags(this auto&& self) noexcept;

    /// NOTE: these are defined in __init__.h
    __declspec(property(get = _get_filename)) Str filename;
    [[nodiscard]] Str _get_filename(this auto&& self);
    __declspec(property(get = _get_name)) Str name;
    [[nodiscard]] Str _get_name(this auto&& self);
    __declspec(property(get = _get_qualname)) Str qualname;
    [[nodiscard]] Str _get_qualname(this auto&& self);
    __declspec(property(get = _get_varnames)) Tuple<Str> varnames;
    [[nodiscard]] Tuple<Str> _get_varnames(this auto&& self);
    __declspec(property(get = _get_cellvars)) Tuple<Str> cellvars;
    [[nodiscard]] Tuple<Str> _get_cellvars(this auto&& self);
    __declspec(property(get = _get_freevars)) Tuple<Str> freevars;
    [[nodiscard]] Tuple<Str> _get_freevars(this auto&& self);
    __declspec(property(get = _get_bytecode)) Bytes bytecode;
    [[nodiscard]] Bytes _get_bytecode(this auto&& self);
    __declspec(property(get = _consts)) Tuple<Object> consts;
    [[nodiscard]] Tuple<Object> _get_consts(this auto&& self);
    __declspec(property(get = _get_names)) Tuple<Str> names;
    [[nodiscard]] Tuple<Str> _get_names(this auto&& self);
};


/* Represents a compiled Python code object, allowing the creation of inline Python
scripts that can be executed from C++.

This class is best explained by example:

    // source.py
    import numpy as np
    print(np.arange(10))

    // main.cpp
    int main() {
        static const auto script = bertrand::Code::compile("source.py");
        script();  // prints [0 1 2 3 4 5 6 7 8 9]
    }

.. note::

    Note that the script in this example is stored in a separate file, which can
    contain arbitrary Python source code.  The file is read and compiled into a
    bytecode object with static storage duration, which persists for the duration of
    the program.

This creates an embedded Python script that can be executed like a normal function.
Here, the script is stateless, and can be executed without context.  Most of the time,
this won't be the case, and data will need to be passed into the script to populate its
namespace.  For instance:

    static const bertrand::Code script = R"(
        print("Hello, " + name + "!")  # name is not defined in this context
    )";

.. note::

    Note the implicit conversion from string to `bertrand::Code`.  This will compile
    the string verbatim, with the only preprocessing being dedentation to align the
    code with the left margin, ignoring blank lines and comments.

If we try to execute this script without a context, we'll get a ``NameError`` just
like normal Python:

    script();  // NameError: name 'name' is not defined

We can solve this by building a context dictionary and passing it into the script as
its global namespace.

    script({{"name", "World"}});  // prints Hello, World!

This uses the ordinary bertrand::Dict constructors, which can take arbitrary C++
objects and pass them seamlessly to Python.  If we want to do the opposite and extract
data from the script back to C++, then we can inspect its return value, which is
another dictionary containing the context after execution.  For instance:

    bertrand::Dict context = bertrand::Code{R"(
        x = 1
        y = 2
        z = 3
    )"}();

    bertrand::print(context);  // prints {"x": 1, "y": 2, "z": 3}

.. note::

    Note that one-off scripts can be executed immediately after construction for
    brevity.  Using static storage allows the script to be compiled once and then
    reused multiple times, without the overhead of recompilation.

Combining these features allows us to create a two-way data pipeline between C++ and
Python:

    bertrand::Int z = bertrand::Code{R"(
        def func(x, y):
            return x + y

        z = func(a, b)
    )"}({{"a", 1}, {"b", 2}})["z"];

    bertrand::print(z);  // prints 3

In this example, data originates in C++, passes through python for processing, and then
returns smoothly to C++ with automatic error propagation, reference counting, and type
conversions at every step.

In the previous example, the input dictionary exists only for the duration of the
script's execution, and is discarded immediately afterwards.  However, it is also
possible to pass a mutable reference to an external dictionary, which will be updated
in-place as the script executes.  This allows multiple scripts to be chained using a
shared context, without ever leaving the Python interpreter.  For instance:

    static const bertrand::Code script1 = R"(
        x = 1
        y = 2
    )";

    static const bertrand::Code script2 = R"(
        z = x + y
        del x, y
    )";

    bertrand::Dict context;
    script1(context);
    script2(context);
    bertrand::print(context);  // prints {"z": 3}

Users can, of course, inspect or modify the context between scripts, either to extract
results or pass new data into the next script in the chain.  This makes it possible to
create arbitrarily complex, mixed-language workflows with minimal fuss.

    bertrand::Dict context = bertrand::Code{R"(
        spam = 0
        eggs = 1
    )"}();

    context["ham"] = std::vector<int>{1, 1, 2, 3, 5, 8, 13, 21, 34, 55};

    std::vector<int> fibonacci = bertrand::Code{R"(
        result = []
        for x in ham:
            spam, eggs = (spam + eggs, spam)
            assert(x == spam)
            result.append(eggs)
    )"}(context)["result"];

    bertrand::print(fibonacci);  // prints [0, 1, 1, 2, 3, 5, 8, 13, 21, 34]

This means that Python can be easily included as an inline scripting language in any
C++ application, with minimal overhead and full compatibility in both directions.  Each
script is evaluated just like an ordinary Python file, and there are no restrictions on
what can be done inside them.  This includes importing modules, defining classes and
functions to be exported back to C++, interacting with the file system, third-party
libraries, client code, and more.  Similarly, it is executed just like normal Python
bytecode, and should not suffer any significant performance penalties beyond copying
data into or out of the context.

    static const bertrand::Code script = R"(
        print(x)
    )";

    script({{"x", "hello"}});
    script({{"x", "from"}});
    script({{"x", "the"}});
    script({{"x", "other"}});
    script({{"x", "side"}});
*/
struct Code : Object, interface<Code> {
    struct __python__ : cls<__python__, Code>, PyCodeObject {
        static Type<Code> __import__();
    };

    Code(PyObject* p, borrowed_t t) : Object(p, t) {}
    Code(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename T = Code> requires (__initializer__<T>::enable)
    Code(std::initializer_list<typename __initializer__<T>::type> init) : Object(
        __initializer__<T>{}(init)
    ) {}

    template <typename... Args> requires (implicit_ctor<Code>::template enable<Args...>)
    Code(Args&&... args) : Object(
        implicit_ctor<Code>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Code>::template enable<Args...>)
    explicit Code(Args&&... args) : Object(
        explicit_ctor<Code>{},
        std::forward<Args>(args)...
    ) {}

};


template <>
struct interface<Type<Code>> {
    [[nodiscard]] static Code compile(const std::string& source) {
        return Code::compile(source);
    }

    template <meta::inherits<interface<Code>> Self>
    [[nodiscard]] static Py_ssize_t line_number(Self&& self) noexcept {
        return std::forward<Self>(self).line_number;
    }
    template <meta::inherits<interface<Code>> Self>
    [[nodiscard]] static Py_ssize_t argcount(Self&& self) noexcept {
        return std::forward<Self>(self).argcount;
    }
    template <meta::inherits<interface<Code>> Self>
    [[nodiscard]] static Py_ssize_t posonlyargcount(Self&& self) noexcept {
        return std::forward<Self>(self).posonlyargcount;
    }
    template <meta::inherits<interface<Code>> Self>
    [[nodiscard]] static Py_ssize_t kwonlyargcount(Self&& self) noexcept {
        return std::forward<Self>(self).kwonlyargcount;
    }
    template <meta::inherits<interface<Code>> Self>
    [[nodiscard]] static Py_ssize_t nlocals(Self&& self) noexcept {
        return std::forward<Self>(self).nlocals;
    }
    template <meta::inherits<interface<Code>> Self>
    [[nodiscard]] static Py_ssize_t stacksize(Self&& self) noexcept {
        return std::forward<Self>(self).stacksize;
    }
    template <meta::inherits<interface<Code>> Self>
    [[nodiscard]] static int flags(Self&& self) noexcept {
        return std::forward<Self>(self).flags;
    }

    /// NOTE: these are defined in __init__.h
    template <meta::inherits<interface<Code>> Self>
    [[nodiscard]] static Str filename(Self&& self);
    template <meta::inherits<interface<Code>> Self>
    [[nodiscard]] static Str name(Self&& self);
    template <meta::inherits<interface<Code>> Self>
    [[nodiscard]] static Str qualname(Self&& self);
    template <meta::inherits<interface<Code>> Self>
    [[nodiscard]] static Tuple<Str> varnames(Self&& self);
    template <meta::inherits<interface<Code>> Self>
    [[nodiscard]] static Tuple<Str> cellvars(Self&& self);
    template <meta::inherits<interface<Code>> Self>
    [[nodiscard]] static Tuple<Str> freevars(Self&& self);
    template <meta::inherits<interface<Code>> Self>
    [[nodiscard]] static Bytes bytecode(Self&& self);
    template <meta::inherits<interface<Code>> Self>
    [[nodiscard]] static Tuple<Object> consts(Self&& self);
    template <meta::inherits<interface<Code>> Self>
    [[nodiscard]] static Tuple<Str> names(Self&& self);
};


template <meta::is<Object> Derived, meta::is<Code> Base>
struct __isinstance__<Derived, Base>                        : returns<bool> {
    static constexpr bool operator()(Derived obj) {
        return PyCode_Check(ptr(obj));
    }
};


/* Implicitly convert a source string into a compiled code object. */
template <std::convertible_to<std::string> Source>
struct __cast__<Source, Code>                               : returns<Code> {
    static auto operator()(const std::string& path);
};


/* Execute the code object with an empty context. */
template <meta::is<Code> Self>
struct __call__<Self>                                       : returns<Dict<Str, Object>> {
    static auto operator()(Self&& self);  // defined in __init__.h
};


/* Execute the code object with a given context, which can be either mutable or a
temporary. */
template <meta::is<Code> Self, std::convertible_to<Dict<Str, Object>> Context>
struct __call__<Self, Context>                              : returns<Dict<Str, Object>> {
    static auto operator()(Self&& self, Dict<Str, Object>& context);  // defined in __init__.h
    static auto operator()(Self&& self, Dict<Str, Object>&& context);  // defined in __init__.h
};


/* Get the first line number of the function. */
[[nodiscard]] inline Py_ssize_t interface<Code>::_get_line_number(this auto&& self) noexcept {
    return view(self)->co_firstlineno;
}


/* Get the number of positional arguments for the function. */
[[nodiscard]] inline Py_ssize_t interface<Code>::_get_argcount(this auto&& self) noexcept {
    return view(self)->co_argcount;
}


/* Get the number of positional-only arguments for the function, including those with
default values.  Does not include variable positional or keyword arguments. */
[[nodiscard]] inline Py_ssize_t interface<Code>::_get_posonlyargcount(this auto&& self) noexcept {
    return view(self)->co_posonlyargcount;
}


/* Get the number of keyword-only arguments for the function, including those with
default values.  Does not include positional-only or variable positional/keyword
arguments. */
[[nodiscard]] inline Py_ssize_t interface<Code>::_get_kwonlyargcount(this auto&& self) noexcept {
    return view(self)->co_kwonlyargcount;
}


/* Get the number of local variables used by the function (including all parameters). */
[[nodiscard]] inline Py_ssize_t interface<Code>::_get_nlocals(this auto&& self) noexcept {
    return view(self)->co_nlocals;
}


/* Get the required stack space for the code object. */
[[nodiscard]] inline Py_ssize_t interface<Code>::_get_stacksize(this auto&& self) noexcept {
    return view(self)->co_stacksize;
}


/* Get an integer encoding flags for the Python interpreter. */
[[nodiscard]] inline int interface<Code>::_get_flags(this auto&& self) noexcept {
    return view(self)->co_flags;
}


///////////////////////////
////    STACK FRAME    ////
///////////////////////////


namespace impl {

    [[nodiscard]] inline std::string parse_function_name(const std::string& name) {
        /* NOTE: functions and classes that accept static strings as template
         * arguments are decomposed into numeric character arrays in the symbol name,
         * which need to be reconstructed here.  Here's an example:
         *
         *      // TODO: find a new example, probably using bertrand::getattr<"append">(list)
         *
         *      File ".../bertrand/python/core/ops.h",
         *      line 268, in bertrand::impl::Attr<bertrand::Object,
         *      bertrand::static_str<7ul>{char [8]{(char)95, (char)95, (char)103,
         *      (char)101, (char)116, (char)95, (char)95}}>::get_attr() const
         *
         * Our goal is to replace the `bertrand::static_str<7ul>{char [8]{...}}`
         * bit with the text it represents, in this case the string `"__get__"`.
         */
        size_t pos = name.find("bertrand::static_str<");
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
            pos = name.find("bertrand::static_str<", pos);
        }
        return result + name.substr(last);
    }

}


struct Frame;


template <>
struct interface<Frame> {
    [[nodiscard]] std::string to_string(this const auto& self);

    __declspec(property(get = _get_code)) std::optional<Code> code;
    [[nodiscard]] std::optional<Code> _get_code(this auto&& self);
    __declspec(property(get = _get_back)) std::optional<Frame> back;
    [[nodiscard]] std::optional<Frame> _get_back(this auto&& self);
    __declspec(property(get = _get_line_number)) size_t line_number;
    [[nodiscard]] size_t _get_line_number(this auto&& self);
    __declspec(property(get = _get_last_instruction)) size_t last_instruction;
    [[nodiscard]] size_t _get_last_instruction(this auto&& self);
    __declspec(property(get = _get_generator)) std::optional<Object> generator;
    [[nodiscard]] std::optional<Object> _get_generator(this auto&& self);

    /// NOTE: these are defined in __init__.h
    [[nodiscard]] Object get(this auto&& self, const Str& name);
    __declspec(property(get = _get_builtins)) Dict<Str, Object> builtins;
    [[nodiscard]] Dict<Str, Object> _get_builtins(this auto&& self);
    __declspec(property(get = _get_globals)) Dict<Str, Object> globals;
    [[nodiscard]] Dict<Str, Object> _get_globals(this auto&& self);
    __declspec(property(get = _get_locals)) Dict<Str, Object> locals;
    [[nodiscard]] Dict<Str, Object> _get_locals(this auto&& self);
};


/* A CPython interpreter frame, which can be introspected or arranged into coherent
cross-language tracebacks. */
struct Frame : Object, interface<Frame> {
    struct __python__ : cls<__python__, Frame>, PyFrameObject {
        static Type<Frame> __import__();
    };

    Frame(PyObject* p, borrowed_t t) : Object(p, t) {}
    Frame(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename T = Frame> requires (__initializer__<T>::enable)
    Frame(std::initializer_list<typename __initializer__<T>::type> init) : Object(
        __initializer__<T>{}(init)
    ) {}

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


template <>
struct interface<Type<Frame>> {
    template <meta::inherits<interface<Frame>> Self>
    [[nodiscard]] static std::string to_string(Self&& self) {
        return std::forward<Self>(self).to_string();
    }
    template <meta::inherits<interface<Frame>> Self>
    [[nodiscard]] static std::optional<Code> code(Self&& self) {
        return std::forward<Self>(self).code;
    }
    template <meta::inherits<interface<Frame>> Self>
    [[nodiscard]] static std::optional<Frame> back(Self&& self) {
        return std::forward<Self>(self).back;
    }
    template <meta::inherits<interface<Frame>> Self>
    [[nodiscard]] static size_t line_number(Self&& self) {
        return std::forward<Self>(self).line_number;
    }
    template <meta::inherits<interface<Frame>> Self>
    [[nodiscard]] static size_t last_instruction(Self&& self) {
        return std::forward<Self>(self).last_instruction;
    }
    template <meta::inherits<interface<Frame>> Self>
    [[nodiscard]] static std::optional<Object> generator(Self&& self) {
        return std::forward<Self>(self).generator;
    }

    /// NOTE: these are defined in __init__.h
    template <meta::inherits<interface<Frame>> Self>
    [[nodiscard]] static Object get(Self&& self, const Str& name);
    template <meta::inherits<interface<Frame>> Self>
    [[nodiscard]] static Dict<Str, Object> builtins(Self&& self);
    template <meta::inherits<interface<Frame>> Self>
    [[nodiscard]] static Dict<Str, Object> globals(Self&& self);
    template <meta::inherits<interface<Frame>> Self>
    [[nodiscard]] static Dict<Str, Object> locals(Self&& self);
};


template <meta::is<Object> Derived, meta::is<Frame> Base>
struct __isinstance__<Derived, Base>                        : returns<bool> {
    static constexpr bool operator()(Derived obj) {
        return PyFrame_Check(ptr(obj));
    }
};


/* Default initializing a Frame object retrieves the currently-executing Python frame,
if one exists.  Note that this frame is guaranteed to have a valid Python bytecode
object, unlike the C++ frames of a Traceback object. */
template <>
struct __init__<Frame>                                      : returns<Frame> {
    static auto operator()();
};


/* Providing an explicit integer will skip that number of frames from either the least
recent Python frame (if positive or zero) or the most recent (if negative).  Like the
default constructor, this always retrieves a frame with a valid Python bytecode object,
unlike the C++ frames of a Traceback object. */
template <std::convertible_to<int> T>
struct __init__<Frame, T>                                   : returns<Frame> {
    static Frame operator()(int skip);
};


template <meta::is<cpptrace::stacktrace_frame> T>
struct __cast__<T>                                          : returns<Frame> {};


/* Converting a `cpptrace::stacktrace_frame` into a Python frame object will synthesize
an interpreter frame with an empty bytecode object. */
template <meta::is<cpptrace::stacktrace_frame> T>
struct __cast__<T, Frame>                                   : returns<Frame> {
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
        return steal<Frame>(reinterpret_cast<PyObject*>(result));
    }
};


/* Execute the bytecode object stored within a Python frame using its current context.
This is the main entry point for the Python interpreter, and causes the program to run
until it either terminates or encounters an error.  The return value is the result of
the last evaluated expression, which can be the return value of a function, the yield
value of a generator, etc. */
template <meta::is<Frame> Self>
struct __call__<Self>                                       : returns<Object> {
    static auto operator()(Self&& frame);
};


///////////////////////////
////    STACK TRACE    ////
///////////////////////////


/// TODO: tracebacks should be either deleted or moved later on in the process.


struct Traceback;


template <>
struct interface<Traceback> {
    [[nodiscard]] std::string to_string(this const auto& self);
};


/* A cross-language traceback that records an accurate call stack of a mixed Python/C++
application. */
struct Traceback : Object, interface<Traceback> {
    struct __python__ : cls<__python__, Traceback>, PyTracebackObject {
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


template <meta::is<cpptrace::stacktrace> T>
struct __cast__<T>                                          : returns<Traceback> {};


/* Converting a `cpptrace::stacktrace_frame` into a Python frame object will synthesize
an interpreter frame with an empty bytecode object. */
template <meta::is<cpptrace::stacktrace> T>
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

        return steal<Traceback>(reinterpret_cast<PyObject*>(front));
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
            return steal<Traceback>(nullptr);
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
                    return steal<Traceback>(nullptr);
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
template <meta::is<Traceback> Self>
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
template <meta::is<Traceback> Self>
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
template <meta::is<Traceback> Self>
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


}


#endif
