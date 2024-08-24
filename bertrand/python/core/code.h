#ifndef BERTRAND_PYTHON_CORE_CODE_H
#define BERTRAND_PYTHON_CORE_CODE_H

#include "declarations.h"
#include "object.h"


namespace py {


///////////////////////
////   BYTECODE    ////
///////////////////////


template <>
struct Type<Code>;


template <>
struct Interface<Code> {
    [[nodiscard]] static Code compile(const std::string& source);

    __declspec(property(get = _line_number)) Py_ssize_t line_number;
    [[nodiscard]] Py_ssize_t _line_number(this const auto& self) noexcept;
    __declspec(property(get = _argcount)) Py_ssize_t argcount;
    [[nodiscard]] Py_ssize_t _argcount(this const auto& self) noexcept;
    __declspec(property(get = _posonlyargcount)) Py_ssize_t posonlyargcount;
    [[nodiscard]] Py_ssize_t _posonlyargcount(this const auto& self) noexcept;
    __declspec(property(get = _kwonlyargcount)) Py_ssize_t kwonlyargcount;
    [[nodiscard]] Py_ssize_t _kwonlyargcount(this const auto& self) noexcept;
    __declspec(property(get = _nlocals)) Py_ssize_t nlocals;
    [[nodiscard]] Py_ssize_t _nlocals(this const auto& self) noexcept;
    __declspec(property(get = _stacksize)) Py_ssize_t stacksize;
    [[nodiscard]] Py_ssize_t _stacksize(this const auto& self) noexcept;
    __declspec(property(get = _flags)) int flags;
    [[nodiscard]] int _flags(this const auto& self) noexcept;

    /// NOTE: these are defined in __init__.h
    __declspec(property(get = _filename)) Str filename;
    [[nodiscard]] Str _filename(this const auto& self);
    __declspec(property(get = _name)) Str name;
    [[nodiscard]] Str _name(this const auto& self);
    __declspec(property(get = _qualname)) Str qualname;
    [[nodiscard]] Str _qualname(this const auto& self);
    __declspec(property(get = _varnames)) Tuple<Str> varnames;
    [[nodiscard]] Tuple<Str> _varnames(this const auto& self);
    __declspec(property(get = _cellvars)) Tuple<Str> cellvars;
    [[nodiscard]] Tuple<Str> _cellvars(this const auto& self);
    __declspec(property(get = _freevars)) Tuple<Str> freevars;
    [[nodiscard]] Tuple<Str> _freevars(this const auto& self);
    __declspec(property(get = _bytecode)) Bytes bytecode;
    [[nodiscard]] Bytes _bytecode(this const auto& self);
    __declspec(property(get = _consts)) Tuple<Object> consts;
    [[nodiscard]] Tuple<Object> _consts(this const auto& self);
    __declspec(property(get = _names)) Tuple<Str> names;
    [[nodiscard]] Tuple<Str> _names(this const auto& self);
};
template <>
struct Interface<Type<Code>> {
    [[nodiscard]] static Code compile(const std::string& source);
    [[nodiscard]] static Py_ssize_t line_number(const auto& self) noexcept;
    [[nodiscard]] static Py_ssize_t argcount(const auto& self) noexcept;
    [[nodiscard]] static Py_ssize_t posonlyargcount(const auto& self) noexcept;
    [[nodiscard]] static Py_ssize_t kwonlyargcount(const auto& self) noexcept;
    [[nodiscard]] static Py_ssize_t nlocals(const auto& self) noexcept;
    [[nodiscard]] static Py_ssize_t stacksize(const auto& self) noexcept;
    [[nodiscard]] static int flags(const auto& self) noexcept;

    /// NOTE: these are defined in __init__.h
    [[nodiscard]] static Str filename(const auto& self);
    [[nodiscard]] static Str name(const auto& self);
    [[nodiscard]] static Str qualname(const auto& self);
    [[nodiscard]] static Tuple<Str> varnames(const auto& self);
    [[nodiscard]] static Tuple<Str> cellvars(const auto& self);
    [[nodiscard]] static Tuple<Str> freevars(const auto& self);
    [[nodiscard]] static Bytes bytecode(const auto& self);
    [[nodiscard]] static Tuple<Object> consts(const auto& self);
    [[nodiscard]] static Tuple<Str> names(const auto& self);
};


/* Represents a compiled Python code object, allowing the creation of inline Python
scripts that can be executed from C++.

This class is best explained by example:

    // source.py
    import numpy as np
    print(np.arange(10))

    // main.cpp
    int main() {
        static const py::Code script = py::Code::compile("source.py");
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

    static const py::Code script = R"(
        print("Hello, " + name + "!")  # name is not defined in this context
    )";

.. note::

    Note the implicit conversion from string to `py::Code`.  This will compile the
    string verbatim, with the only preprocessing being dedentation to align the code
    with the left margin, ignoring blank lines and comments.

If we try to execute this script without a context, we'll get a ``NameError`` just
like normal Python:

    script();  // NameError: name 'name' is not defined

We can solve this by building a context dictionary and passing it into the script as
its global namespace.

    script({{"name", "World"}});  // prints Hello, World!

This uses the ordinary py::Dict constructors, which can take arbitrary C++ objects and
pass them seamlessly to Python.  If we want to do the opposite and extract data from
the script back to C++, then we can inspect its return value, which is another
dictionary containing the context after execution.  For instance:

    py::Dict context = py::Code{R"(
        x = 1
        y = 2
        z = 3
    )"}();

    py::print(context);  // prints {"x": 1, "y": 2, "z": 3}

.. note::

    Note that one-off scripts can be executed immediately after construction for
    brevity.  Using static storage allows the script to be compiled once and then
    reused multiple times, without the overhead of recompilation.

Combining these features allows us to create a two-way data pipeline between C++ and
Python:

    py::Int z = py::Code{R"(
        def func(x, y):
            return x + y

        z = func(a, b)
    )"}({{"a", 1}, {"b", 2}})["z"];

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
    )";

    static const py::Code script2 = R"(
        z = x + y
        del x, y
    )";

    py::Dict context;
    script1(context);
    script2(context);
    py::print(context);  // prints {"z": 3}

Users can, of course, inspect or modify the context between scripts, either to extract
results or pass new data into the next script in the chain.  This makes it possible to
create arbitrarily complex, mixed-language workflows with minimal fuss.

    py::Dict context = py::Code{R"(
        spam = 0
        eggs = 1
    )"}();

    context["ham"] = std::vector<int>{1, 1, 2, 3, 5, 8, 13, 21, 34, 55};

    std::vector<int> fibonacci = py::Code{R"(
        result = []
        for x in ham:
            spam, eggs = (spam + eggs, spam)
            assert(x == spam)
            result.append(eggs)
    )"}(context)["result"];

    py::print(fibonacci);  // prints [0, 1, 1, 2, 3, 5, 8, 13, 21, 34]

This means that Python can be easily included as an inline scripting language in any
C++ application, with minimal overhead and full compatibility in both directions.  Each
script is evaluated just like an ordinary Python file, and there are no restrictions on
what can be done inside them.  This includes importing modules, defining classes and
functions to be exported back to C++, interacting with the file system, third-party
libraries, client code, and more.  Similarly, it is executed just like normal Python
bytecode, and should not suffer any significant performance penalties beyond copying
data into or out of the context.

    static const py::Code script = R"(
        print(x)
    )";

    script({{"x", "hello"}});
    script({{"x", "from"}});
    script({{"x", "the"}});
    script({{"x", "other"}});
    script({{"x", "side"}});
*/
struct Code : Object, Interface<Code> {
    struct __python__ : def<__python__, Code>, PyCodeObject {
        static Type<Code> __import__();
    };

    Code(PyObject* p, borrowed_t t) : Object(p, t) {}
    Code(PyObject* p, stolen_t t) : Object(p, t) {}

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
struct Type<Code> : Object, Interface<Type<Code>> {
    struct __python__ : def<__python__, Type>, PyTypeObject {
        static Type __import__() {
            return Code::__python__::__import__();
        }
    };

    Type(PyObject* p, borrowed_t t) : Object(p, t) {}
    Type(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}
};


inline Type<Code> Code::__python__::__import__() {
    return reinterpret_borrow<Type<Code>>(reinterpret_cast<PyObject*>(&PyCode_Type));
}


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
[[nodiscard]] inline Py_ssize_t Interface<Code>::_line_number(
    this const auto& self
) noexcept {
    return reinterpret_cast<PyCodeObject*>(ptr(self))->co_firstlineno;
}


/* Get the number of positional arguments for the function. */
[[nodiscard]] inline Py_ssize_t Interface<Code>::_argcount(
    this const auto& self
) noexcept {
    return reinterpret_cast<PyCodeObject*>(ptr(self))->co_argcount;
}


/* Get the number of positional-only arguments for the function, including those with
default values.  Does not include variable positional or keyword arguments. */
[[nodiscard]] inline Py_ssize_t Interface<Code>::_posonlyargcount(
    this const auto& self
) noexcept {
    return reinterpret_cast<PyCodeObject*>(ptr(self))->co_posonlyargcount;
}


/* Get the number of keyword-only arguments for the function, including those with
default values.  Does not include positional-only or variable positional/keyword
arguments. */
[[nodiscard]] inline Py_ssize_t Interface<Code>::_kwonlyargcount(
    this const auto& self
) noexcept {
    return reinterpret_cast<PyCodeObject*>(ptr(self))->co_kwonlyargcount;
}


/* Get the number of local variables used by the function (including all parameters). */
[[nodiscard]] inline Py_ssize_t Interface<Code>::_nlocals(
    this const auto& self
) noexcept {
    return reinterpret_cast<PyCodeObject*>(ptr(self))->co_nlocals;
}


/* Get the required stack space for the code object. */
[[nodiscard]] inline Py_ssize_t Interface<Code>::_stacksize(
    this const auto& self
) noexcept {
    return reinterpret_cast<PyCodeObject*>(ptr(self))->co_stacksize;
}


/* Get an integer encoding flags for the Python interpreter. */
[[nodiscard]] inline int Interface<Code>::_flags(
    this const auto& self
) noexcept {
    return reinterpret_cast<PyCodeObject*>(ptr(self))->co_flags;
}


[[nodiscard]] inline Code Interface<Type<Code>>::compile(const std::string& source) {
    return Code::compile(source);
}
[[nodiscard]] inline Py_ssize_t Interface<Type<Code>>::line_number(const auto& self) noexcept {
    return self.line_number;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<Code>>::argcount(const auto& self) noexcept {
    return self.argcount;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<Code>>::posonlyargcount(const auto& self) noexcept {
    return self.posonlyargcount;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<Code>>::kwonlyargcount(const auto& self) noexcept {
    return self.kwonlyargcount;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<Code>>::nlocals(const auto& self) noexcept {
    return self.nlocals;
}
[[nodiscard]] inline Py_ssize_t Interface<Type<Code>>::stacksize(const auto& self) noexcept {
    return self.stacksize;
}
[[nodiscard]] inline int Interface<Type<Code>>::flags(const auto& self) noexcept {
    return self.flags;
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


template <>
struct Type<Frame>;


template <>
struct Interface<Frame> {
    [[nodiscard]] std::string to_string(this const auto& self);

    __declspec(property(get = _code)) std::optional<Code> code;
    [[nodiscard]] std::optional<Code> _code(this const auto& self);
    __declspec(property(get = _back)) std::optional<Frame> back;
    [[nodiscard]] std::optional<Frame> _back(this const auto& self);
    __declspec(property(get = _line_number)) size_t line_number;
    [[nodiscard]] size_t _line_number(this const auto& self);
    __declspec(property(get = _last_instruction)) size_t last_instruction;
    [[nodiscard]] size_t _last_instruction(this const auto& self);
    __declspec(property(get = _generator)) std::optional<Object> generator;
    [[nodiscard]] std::optional<Object> _generator(this const auto& self);

    /// NOTE: these are defined in __init__.h
    [[nodiscard]] Object get(this const auto& self, const Str& name);
    __declspec(property(get = _builtins)) Dict<Str, Object> builtins;
    [[nodiscard]] Dict<Str, Object> _builtins(this const auto& self);
    __declspec(property(get = _globals)) Dict<Str, Object> globals;
    [[nodiscard]] Dict<Str, Object> _globals(this const auto& self);
    __declspec(property(get = _locals)) Dict<Str, Object> locals;
    [[nodiscard]] Dict<Str, Object> _locals(this const auto& self);
};
template <>
struct Interface<Type<Frame>> {
    [[nodiscard]] static std::string to_string(const auto& self);
    [[nodiscard]] static std::optional<Code> code(const auto& self);
    [[nodiscard]] static std::optional<Frame> back(const auto& self);
    [[nodiscard]] static size_t line_number(const auto& self);
    [[nodiscard]] static size_t last_instruction(const auto& self);
    [[nodiscard]] static std::optional<Object> generator(const auto& self);

    /// NOTE: these are defined in __init__.h
    [[nodiscard]] static Object get(const auto& self, const Str& name);
    [[nodiscard]] static Dict<Str, Object> builtins(const auto& self);
    [[nodiscard]] static Dict<Str, Object> globals(const auto& self);
    [[nodiscard]] static Dict<Str, Object> locals(const auto& self);
};


/* A CPython interpreter frame, which can be introspected or arranged into coherent
cross-language tracebacks. */
struct Frame : Object, Interface<Frame> {
    struct __python__ : def<__python__, Frame>, PyFrameObject {
        static Type<Frame> __import__();
    };

    Frame(PyObject* p, borrowed_t t) : Object(p, t) {}
    Frame(PyObject* p, stolen_t t) : Object(p, t) {}

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
struct Type<Frame> : Object, Interface<Type<Frame>> {
    struct __python__ : def<__python__, Type>, PyTypeObject {
        static Type __import__() {
            return Frame::__python__::__import__();
        }
    };

    Type(PyObject* p, borrowed_t t) : Object(p, t) {}
    Type(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename... Args> requires (implicit_ctor<Type>::enable<Args...>)
    Type(Args&&... args) : Object(
        implicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}

    template <typename... Args> requires (explicit_ctor<Type>::enable<Args...>)
    explicit Type(Args&&... args) : Object(
        explicit_ctor<Type>{},
        std::forward<Args>(args)...
    ) {}
};


inline Type<Frame> Frame::__python__::__import__() {
    return reinterpret_borrow<Type<Frame>>(
        reinterpret_cast<PyObject*>(&PyFrame_Type)
    );
}


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


[[nodiscard]] inline std::string Interface<Type<Frame>>::to_string(const auto& self) {
    return self.to_string();
}
[[nodiscard]] inline std::optional<Code> Interface<Type<Frame>>::code(const auto& self) {
    return self.code;
}
[[nodiscard]] inline std::optional<Frame> Interface<Type<Frame>>::back(const auto& self) {
    return self.back;
}
[[nodiscard]] inline size_t Interface<Type<Frame>>::line_number(const auto& self) {
    return self.line_number;
}
[[nodiscard]] inline size_t Interface<Type<Frame>>::last_instruction(const auto& self) {
    return self.last_instruction;
}
[[nodiscard]] inline std::optional<Object> Interface<Type<Frame>>::generator(const auto& self) {
    return self.generator;
}


}


#endif
