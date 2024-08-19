#ifndef BERTRAND_PYTHON_CORE_CODE_H
#define BERTRAND_PYTHON_CORE_CODE_H

#include "declarations.h"
#include "object.h"


namespace py {


///////////////////////
////   BYTECODE    ////
///////////////////////


template <>
struct Interface<Code> {
    [[nodiscard]] static Code compile(const std::string& source);

    __declspec(property(get = _line_number)) Py_ssize_t line_number;
    [[nodiscard]] Py_ssize_t _line_number() const noexcept;
    __declspec(property(get = _argcount)) Py_ssize_t argcount;
    [[nodiscard]] Py_ssize_t _argcount() const noexcept;
    __declspec(property(get = _posonlyargcount)) Py_ssize_t posonlyargcount;
    [[nodiscard]] Py_ssize_t _posonlyargcount() const noexcept;
    __declspec(property(get = _kwonlyargcount)) Py_ssize_t kwonlyargcount;
    [[nodiscard]] Py_ssize_t _kwonlyargcount() const noexcept;
    __declspec(property(get = _nlocals)) Py_ssize_t nlocals;
    [[nodiscard]] Py_ssize_t _nlocals() const noexcept;
    __declspec(property(get = _stacksize)) Py_ssize_t stacksize;
    [[nodiscard]] Py_ssize_t _stacksize() const noexcept;
    __declspec(property(get = _flags)) int flags;
    [[nodiscard]] int _flags() const noexcept;

    /// NOTE: these are defined in __init__.h
    __declspec(property(get = _filename)) Str filename;
    [[nodiscard]] Str _filename() const;
    __declspec(property(get = _name)) Str name;
    [[nodiscard]] Str _name() const;
    __declspec(property(get = _qualname)) Str qualname;
    [[nodiscard]] Str _qualname() const;
    __declspec(property(get = _varnames)) Tuple<Str> varnames;
    [[nodiscard]] Tuple<Str> _varnames() const;
    __declspec(property(get = _cellvars)) Tuple<Str> cellvars;
    [[nodiscard]] Tuple<Str> _cellvars() const;
    __declspec(property(get = _freevars)) Tuple<Str> freevars;
    [[nodiscard]] Tuple<Str> _freevars() const;
    __declspec(property(get = _bytecode)) Bytes bytecode;
    [[nodiscard]] Bytes _bytecode() const;
    __declspec(property(get = _consts)) Tuple<Object> consts;
    [[nodiscard]] Tuple<Object> _consts() const;
    __declspec(property(get = _names)) Tuple<Str> names;
    [[nodiscard]] Tuple<Str> _names() const;
};


/* Represents a compiled Python code object, enabling seamless embedding of Python as a
scripting language within C++.

This class is extremely powerful, and is best explained by example:

    // in source.py
    import numpy as np
    print(np.arange(10))

    // in main.cpp
    static const py::Code script("source.py");
    script();  // prints [0 1 2 3 4 5 6 7 8 9]

.. note::

    Note that the script in this example is stored in a separate file, which can
    contain arbitrary Python source code.  The file is read and compiled into an
    interactive code object with static storage duration, which is cached for the
    duration of the program.

This creates an embedded Python script that can be executed like a normal function.
Here, the script is stateless, and can be executed without context.  Most of the time,
this won't be the case, and data will need to be passed into the script to populate its
namespace.  For instance:

    static const py::Code script = R"(
        print("Hello, " + name + "!")  # name is not defined in this context
    )"_python;

.. note::

    Note the user-defined `_python` literal used to create the script.  This is
    equivalent to calling the `Code` constructor on a separate file, but allows the
    script to be written directly within the C++ source code.  The same effect can be
    achieved via the `Code::compile()` helper method if the `py::literals` namespace is
    not available. 

If we try to execute this script without a context, we'll get a ``NameError`` just
like normal Python:

    script();  // NameError: name 'name' is not defined

We can solve this by building a context dictionary and passing it into the script as
its global namespace.

    script({{"name", "World"}});  // prints Hello, World!

This uses the ordinary py::Dict constructors, which can take arbitrary C++ objects and
pass them seamlessly to Python.  If we want to do the opposite and extract data from
the script back to C++, then we can use its return value, which is another dictionary
containing the context after execution.  For instance:

    py::Dict context = R"(
        x = 1
        y = 2
        z = 3
    )"_python();

    py::print(context);  // prints {"x": 1, "y": 2, "z": 3}

Combining these features allows us to create a two-way data pipeline between C++ and
Python:

    py::Int z = R"(
        def func(x, y):
            return x + y

        z = func(a, b)
    )"_python({{"a", 1}, {"b", 2}})["z"];

    py::print(z);  // prints 3

In this example, data originates in C++, passes through python for processing, and then
returns smoothly to C++ with automatic error propagation, reference counting, and type
conversions at every step.

In the previous example, the input dictionary exists only for the duration of the
script's execution, and is discarded immediately afterwards.  However, it is also
possible to pass a mutable reference to an external dictionary, which will be updated
in-place as the script executes.  This allows multiple scripts to be chained using a
shared context, without ever leaving the Python interpreter.  For instance:

    static const py::Code script1 = R"(
        x = 1
        y = 2
    )"_python;

    static const py::Code script2 = R"(
        z = x + y
        del x, y
    )"_python;

    py::Dict context;
    script1(context);
    script2(context);
    py::print(context);  // prints {"z": 3}

Users can, of course, inspect or modify the context between scripts, either to extract
results or pass new data into the next script in the chain.  This makes it possible to
create arbitrarily complex, mixed-language workflows with minimal fuss.

    py::Dict context = R"(
        spam = 0
        eggs = 1
    )"_python();

    context["ham"] = std::vector<int>{1, 1, 2, 3, 5, 8, 13, 21, 34, 55};

    std::vector<int> fibonacci = R"(
        result = []
        for x in ham:
            spam, eggs = (spam + eggs, spam)
            assert(x == spam)
            result.append(eggs)
    )"_python(context)["result"];

    py::print(fibonacci);  // prints [0, 1, 1, 2, 3, 5, 8, 13, 21, 34]

This means that Python can be easily included as an inline scripting language in any
C++ application, with minimal overhead and full compatibility in both directions.  Each
script is evaluated just like an ordinary Python file, and there are no restrictions on
what can be done inside them.  This includes importing modules, defining classes and
functions to be exported back to C++, interacting with the file system, third-party
libraries, client code, and more.  Similarly, it is executed just like normal Python
bytecode, and should not suffer any significant performance penalties beyond copying
data into or out of the context.  This is especially true for static code objects,
which are compiled once and then cached for repeated use.

    static const py::Code script = R"(
        print(x)
    )"_python;

    script({{"x", "hello"}});
    script({{"x", "from"}});
    script({{"x", "the"}});
    script({{"x", "other"}});
    script({{"x", "side"}});
*/
struct Code : Object, Interface<Code> {

    Code(Handle h, borrowed_t t) : Object(h, t) {}
    Code(Handle h, stolen_t t) : Object(h, t) {}

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


template <typename T>
struct __isinstance__<T, Code>                              : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::dynamic_type<T>) {
            return PyCode_Check(ptr(obj));
        } else {
            return issubclass<T, Code>();
        }
    }
};


template <typename T>
struct __issubclass__<T, Code>                              : Returns<bool> {
    static consteval bool operator()() { return std::derived_from<T, Interface<Code>>; }
};


/* Implicitly convert a source string into a compiled code object. */
template <typename Source>
    requires (std::convertible_to<std::decay_t<Source>, std::string>)
struct __init__<Code, Source>                               : Returns<Code> {
    static auto operator()(const std::string& path);
};


/* Execute the code object with an empty context. */
template <>
struct __call__<Code>                                       : Returns<Dict<Str, Object>> {
    static auto operator()(const Code& code);  // defined in __init__.h
};


/* Execute the code object with a given context, which can be either mutable or a
temporary. */
template <std::convertible_to<Dict<Str, Object>> Context>
struct __call__<Code, Context>                              : Returns<Dict<Str, Object>> {
    static auto operator()(const Code& code, Dict<Str, Object>& context);  // defined in __init__.h
    static auto operator()(const Code& code, Dict<Str, Object>&& context);  // defined in __init__.h
};


/* Get the first line number of the function. */
[[nodiscard]] inline Py_ssize_t Interface<Code>::_line_number() const noexcept {
    return reinterpret_cast<PyCodeObject*>(
        ptr(reinterpret_cast<const Object&>(*this))
    )->co_firstlineno;
}


/* Get the number of positional arguments for the function. */
[[nodiscard]] inline Py_ssize_t Interface<Code>::_argcount() const noexcept {
    return reinterpret_cast<PyCodeObject*>(
        ptr(reinterpret_cast<const Object&>(*this))
    )->co_argcount;
}


/* Get the number of positional-only arguments for the function, including those with
default values.  Does not include variable positional or keyword arguments. */
[[nodiscard]] inline Py_ssize_t Interface<Code>::_posonlyargcount() const noexcept {
    return reinterpret_cast<PyCodeObject*>(
        ptr(reinterpret_cast<const Object&>(*this))
    )->co_posonlyargcount;
}


/* Get the number of keyword-only arguments for the function, including those with
default values.  Does not include positional-only or variable positional/keyword
arguments. */
[[nodiscard]] inline Py_ssize_t Interface<Code>::_kwonlyargcount() const noexcept {
    return reinterpret_cast<PyCodeObject*>(
        ptr(reinterpret_cast<const Object&>(*this))
    )->co_kwonlyargcount;
}


/* Get the number of local variables used by the function (including all parameters). */
[[nodiscard]] inline Py_ssize_t Interface<Code>::_nlocals() const noexcept {
    return reinterpret_cast<PyCodeObject*>(
        ptr(reinterpret_cast<const Object&>(*this))
    )->co_nlocals;
}


/* Get the required stack space for the code object. */
[[nodiscard]] inline Py_ssize_t Interface<Code>::_stacksize() const noexcept {
    return reinterpret_cast<PyCodeObject*>(
        ptr(reinterpret_cast<const Object&>(*this))
    )->co_stacksize;
}


/* Get an integer encoding flags for the Python interpreter. */
[[nodiscard]] inline int Interface<Code>::_flags() const noexcept {
    return reinterpret_cast<PyCodeObject*>(
        ptr(reinterpret_cast<const Object&>(*this))
    )->co_flags;
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
         *      // TODO: find a new example, probably using py::getattr<"append">(list)
         *
         *      File ".../bertrand/python/core/ops.h",
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


struct Frame;


template <>
struct Interface<Frame> {
    [[nodiscard]] std::string to_string() const;

    __declspec(property(get = _code)) std::optional<Code> code;
    [[nodiscard]] std::optional<Code> _code() const;
    __declspec(property(get = _back)) std::optional<Frame> back;
    [[nodiscard]] std::optional<Frame> _back() const;
    __declspec(property(get = _line_number)) size_t line_number;
    [[nodiscard]] size_t _line_number() const;
    __declspec(property(get = _last_instruction)) size_t last_instruction;
    [[nodiscard]] size_t _last_instruction() const;
    __declspec(property(get = _generator)) std::optional<Object> generator;
    [[nodiscard]] std::optional<Object> _generator() const;

    /// NOTE: these are defined in __init__.h
    [[nodiscard]] Object get(const Str& name) const;
    __declspec(property(get = _builtins)) Dict<Str, Object> builtins;
    [[nodiscard]] Dict<Str, Object> _builtins() const;
    __declspec(property(get = _globals)) Dict<Str, Object> globals;
    [[nodiscard]] Dict<Str, Object> _globals() const;
    __declspec(property(get = _locals)) Dict<Str, Object> locals;
    [[nodiscard]] Dict<Str, Object> _locals() const;
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


template <typename T>
struct __isinstance__<T, Frame>                             : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::dynamic_type<T>) {
            return PyFrame_Check(ptr(obj));
        } else {
            return issubclass<T, Frame>();
        }
    }
};


template <typename T>
struct __issubclass__<T, Frame>                             : Returns<bool> {
    static consteval bool operator()() { return std::derived_from<T, Interface<Frame>>; }
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


}


#endif
