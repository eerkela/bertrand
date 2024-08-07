#ifndef BERTRAND_PYTHON_CODE_H
#define BERTRAND_PYTHON_CODE_H

#include "common.h"
#include "dict.h"
#include "str.h"
#include "bytes.h"
#include "tuple.h"
#include "list.h"
#include "type.h"


namespace py {


////////////////////
////    CODE    ////
////////////////////


template <typename T>
struct __issubclass__<T, Code>                              : Returns<bool> {
    static consteval bool operator()() { return std::derived_from<T, Code>; }
};


template <typename T>
struct __isinstance__<T, Code>                              : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::is_object_exact<T>) {
            return PyCode_Check(ptr(obj));
        } else {
            return issubclass<T, Code>();
        }
    }
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
class Code : public Object {
    using Base = Object;
    using Self = Code;

    PyCodeObject* self() const {
        return reinterpret_cast<PyCodeObject*>(this->ptr());
    }

    template <typename T>
    static PyObject* build(const T& text) {
        std::string line;
        std::string parsed;
        std::istringstream stream(text);
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
            std::istringstream stream2(text);
            while (std::getline(stream2, line)) {
                if (line.empty() || line.find_first_not_of(" \t") == std::string::npos) {
                    temp += '\n';
                } else {
                    temp += line.substr(min_indent) + '\n';
                }
            }
            parsed = temp;
        } else {
            parsed = text;
        }

        PyObject* result = Py_CompileString(
            parsed.c_str(),
            "<embedded Python script>",
            Py_file_input
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return result;
    }

    static PyObject* load(const char* path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw FileNotFoundError(std::string("'") + path + "'");
        }
        std::istreambuf_iterator<char> begin(file), end;
        PyObject* result = Py_CompileString(
            std::string(begin, end).c_str(),
            path,
            Py_file_input
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return result;
    }

public:
    static const Type type;

    Code(Handle h, borrowed_t t) : Base(h, t) {}
    Code(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Code, __init__<Code, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Code, std::remove_cvref_t<Args>...>::enable
        )
    Code(Args&&... args) : Base((
        Interpreter::init(),
        __init__<Code, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<Code, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Code, __explicit_init__<Code, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Code, std::remove_cvref_t<Args>...>::enable
        )
    explicit Code(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<Code, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    /* Parse and compile a source string into a Python code object. */
    [[nodiscard]] static Code compile(const char* source) {
        return reinterpret_steal<Code>(build(source));
    }

    /* Parse and compile a source string into a Python code object. */
    [[nodiscard]] static Code compile(const std::string& source) {
        return reinterpret_steal<Code>(build(source));
    }

    /* Parse and compile a source string into a Python code object. */
    [[nodiscard]] static Code compile(const std::string_view& path) {
        return compile(std::string{path.data(), path.size()});
    }

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Execute the code object without context. */
    BERTRAND_NOINLINE Dict<Str, Object> operator()() const {
        py::Dict<Str, Object> context;
        PyObject* result = PyEval_EvalCode(this->ptr(), context.ptr(), context.ptr());
        if (result == nullptr) {
            Exception::from_python(1);
        }
        Py_DECREF(result);  // always None
        return context;
    }

    /* Execute the code object with the given context. */
    BERTRAND_NOINLINE Dict<Str, Object>& operator()(Dict<Str, Object>& context) const {
        PyObject* result = PyEval_EvalCode(this->ptr(), context.ptr(), context.ptr());
        if (result == nullptr) {
            Exception::from_python(1);
        }
        Py_DECREF(result);  // always None
        return context;
    }

    /* Execute the code object with the given context. */
    BERTRAND_NOINLINE Dict<Str, Object> operator()(Dict<Str, Object>&& context) const {
        PyObject* result = PyEval_EvalCode(this->ptr(), context.ptr(), context.ptr());
        if (result == nullptr) {
            Exception::from_python(1);
        }
        Py_DECREF(result);  // always None
        return context;
    }

    /////////////////////
    ////    SLOTS    ////
    /////////////////////

    /* Get the name of the file from which the code was compiled. */
    __declspec(property(get = _get_filename)) Str filename;
    [[nodiscard]] Str _get_filename() const {
        return reinterpret_borrow<Str>(self()->co_filename);
    }

    /* Get the function's base name. */
    __declspec(property(get = _get_name)) Str name;
    [[nodiscard]] Str _get_name() const {
        return reinterpret_borrow<Str>(self()->co_name);
    }

    /* Get the function's qualified name. */
    __declspec(property(get = _get_qualname)) Str qualname;
    [[nodiscard]] Str _get_qualname() const {
        return reinterpret_borrow<Str>(self()->co_qualname);
    }

    /* Get the first line number of the function. */
    __declspec(property(get = _get_line_number)) Py_ssize_t line_number;
    [[nodiscard]] Py_ssize_t _get_line_number() const noexcept {
        return self()->co_firstlineno;
    }

    /* Get the total number of positional arguments for the function, including
    positional-only arguments and those with default values (but not variable
    or keyword-only arguments). */
    __declspec(property(get = _get_argcount)) Py_ssize_t argcount;
    [[nodiscard]] Py_ssize_t _get_argcount() const noexcept {
        return self()->co_argcount;
    }

    /* Get the number of positional-only arguments for the function, including
    those with default values.  Does not include variable positional or keyword
    arguments. */
    __declspec(property(get = _get_posonlyargcount)) Py_ssize_t posonlyargcount;
    [[nodiscard]] Py_ssize_t _get_posonlyargcount() const noexcept {
        return self()->co_posonlyargcount;
    }

    /* Get the number of keyword-only arguments for the function, including those
    with default values.  Does not include positional-only or variable
    positional/keyword arguments. */
    __declspec(property(get = _get_kwonlyargcount)) Py_ssize_t kwonlyargcount;
    [[nodiscard]] Py_ssize_t _get_kwonlyargcount() const noexcept {
        return self()->co_kwonlyargcount;
    }

    /* Get the number of local variables used by the function (including all
    parameters). */
    __declspec(property(get = _get_nlocals)) Py_ssize_t nlocals;
    [[nodiscard]] Py_ssize_t _get_nlocals() const noexcept {
        return self()->co_nlocals;
    }

    /* Get a tuple containing the names of the local variables in the function,
    starting with parameter names. */
    __declspec(property(get = _get_varnames)) Tuple<Str> varnames;
    [[nodiscard]] Tuple<Str> _get_varnames() const {
        return getattr<"co_varnames">(*this);
    }

    /* Get a tuple containing the names of local variables that are referenced by
    nested functions within this function (i.e. those that are stored in a
    PyCell). */
    __declspec(property(get = _get_cellvars)) Tuple<Str> cellvars;
    [[nodiscard]] Tuple<Str> _get_cellvars() const {
        return getattr<"co_cellvars">(*this);
    }

    /* Get a tuple containing the names of free variables in the function (i.e.
    those that are not stored in a PyCell). */
    __declspec(property(get = _get_freevars)) Tuple<Str> freevars;
    [[nodiscard]] Tuple<Str> _get_freevars() const {
        return getattr<"co_freevars">(*this);
    }

    /* Get the required stack space for the code object. */
    __declspec(property(get = _get_stacksize)) Py_ssize_t stacksize;
    [[nodiscard]] Py_ssize_t _get_stacksize() const noexcept {
        return self()->co_stacksize;
    }

    /* Get the bytecode buffer representing the sequence of instructions in the
    function. */
    __declspec(property(get = _get_bytecode)) Bytes bytecode;
    [[nodiscard]] Bytes _get_bytecode() const {
        return getattr<"co_code">(*this);
    }

    /* Get a tuple containing the literals used by the bytecode in the function. */
    __declspec(property(get = _get_consts)) Tuple<Object> consts;
    [[nodiscard]] Tuple<Object> _get_consts() const {
        return reinterpret_borrow<Tuple<Object>>(self()->co_consts);
    }

    /* Get a tuple containing the names used by the bytecode in the function. */
    __declspec(property(get = _get_names)) Tuple<Str> names;
    [[nodiscard]] Tuple<Str> _get_names() const {
        return reinterpret_borrow<Tuple<Str>>(self()->co_names);
    }

    /* Get an integer encoding flags for the Python interpreter. */
    __declspec(property(get = _get_flags)) int flags;
    [[nodiscard]] int _get_flags() const noexcept {
        return self()->co_flags;
    }

};


// TODO: swap semantics of constructor and compile() method.  Constructor should allow
// compilation of inline scripts, so that you don't need to use the _python literal.
// The ::compile() method would then take a file name and compile the contents of
// into a code object.

// static const py::Code script = R"(...)"
// static const auto script = py::Code::compile("source.py");


template <>
struct __explicit_init__<Code, const char*>                 : Returns<Code> {
    static PyObject* load(const char* path) {
        std::ifstream file(path);
        if (!file.is_open()) {
            throw FileNotFoundError(std::string("'") + path + "'");
        }
        std::istreambuf_iterator<char> begin(file), end;
        PyObject* result = Py_CompileString(
            std::string(begin, end).c_str(),
            path,
            Py_file_input
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return result;
    }
    static auto operator()(const char* path) {
        return reinterpret_steal<Code>(load(path));
    }
};
template <size_t N>
struct __explicit_init__<Code, char[N]>                     : Returns<Code> {
    static auto operator()(const char* path) { return Code(path); }
};
template <>
struct __explicit_init__<Code, std::string>                 : Returns<Code> {
    static auto operator()(const std::string& path) { return Code(path.c_str()); }
};
template <>
struct __explicit_init__<Code, std::string_view>            : Returns<Code> {
    static auto operator()(const std::string_view& path) { return Code(path.data()); }
};


/////////////////////
////    FRAME    ////
/////////////////////


template <typename T>
struct __issubclass__<T, Frame>                              : Returns<bool> {
    static consteval bool operator()() { return std::derived_from<T, Frame>; }
};


template <typename T>
struct __isinstance__<T, Frame>                              : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if constexpr (impl::is_object_exact<T>) {
            return PyFrame_Check(ptr(obj));
        } else {
            return issubclass<T, Frame>();
        }
    }
};


/* Represents a statically-typed Python frame object in C++.  These are the same frames
returned by the `inspect` module and listed in exception tracebacks.  They can be used
to run Python code in an interactive loop via the embedded code object. */
class Frame : public Object {
    using Base = Object;
    using Self = Frame;

    PyFrameObject* self() const {
        return reinterpret_cast<PyFrameObject*>(this->ptr());
    }

public:
    static const Type type;

    Frame(Handle h, borrowed_t t) : Base(h, t) {}
    Frame(Handle h, stolen_t t) : Base(h, t) {}

    template <typename... Args>
        requires (
            std::is_invocable_r_v<Frame, __init__<Frame, std::remove_cvref_t<Args>...>, Args...> &&
            __init__<Frame, std::remove_cvref_t<Args>...>::enable
        )
    Frame(Args&&... args) : Base((
        Interpreter::init(),
        __init__<Frame, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    template <typename... Args>
        requires (
            !__init__<Frame, std::remove_cvref_t<Args>...>::enable &&
            std::is_invocable_r_v<Frame, __explicit_init__<Frame, std::remove_cvref_t<Args>...>, Args...> &&
            __explicit_init__<Frame, std::remove_cvref_t<Args>...>::enable
        )
    explicit Frame(Args&&... args) : Base((
        Interpreter::init(),
        __explicit_init__<Frame, std::remove_cvref_t<Args>...>{}(std::forward<Args>(args)...)
    )) {}

    /* Get the next outer frame from this one. */
    [[nodiscard]] Frame back() const {
        PyFrameObject* result = PyFrame_GetBack(self());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Frame>(reinterpret_cast<PyObject*>(result));
    }

    /* Get the code object associated with this frame. */
    [[nodiscard]] Code code() const {
        return reinterpret_steal<Code>(
            reinterpret_cast<PyObject*>(PyFrame_GetCode(self()))  // never null
        );
    }

    /* Get the line number that the frame is currently executing. */
    [[nodiscard]] int line_number() const noexcept {
        return PyFrame_GetLineNumber(self());
    }

    /* Execute the code object stored within the frame using its current context.  This
    is the main entry point for the Python interpreter, and is used behind the scenes
    whenever a program is run. */
    Object operator()() const {
        PyObject* result = PyEval_EvalFrame(self());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Object>(result);
    }

    #if (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 11)

        /* Get the frame's builtin namespace. */
        [[nodiscard]] Dict<Str, Object> builtins() const {
            return reinterpret_steal<Dict<Str, Object>>(
                PyFrame_GetBuiltins(self())
            );
        }

        /* Get the frame's globals namespace. */
        [[nodiscard]] Dict<Str, Object> globals() const {
            PyObject* result = PyFrame_GetGlobals(self());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Dict<Str, Object>>(result);
        }

        /* Get the frame's locals namespace. */
        [[nodiscard]] Dict<Str, Object> locals() const {
            PyObject* result = PyFrame_GetLocals(self());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Dict<Str, Object>>(result);
        }

        /* Get the generator, coroutine, or async generator that owns this frame, or
        nullopt if this frame is not owned by a generator. */
        [[nodiscard]] std::optional<Object> generator() const {
            PyObject* result = PyFrame_GetGenerator(self());
            if (result == nullptr) {
                return std::nullopt;
            } else {
                return std::make_optional(reinterpret_steal<Object>(result));
            }
        }

        /* Get the "precise instruction" of the frame object, which is an index into
        the bytecode of the last instruction that was executed by the frame's code
        object. */
        [[nodiscard]] int last_instruction() const noexcept {
            return PyFrame_GetLasti(self());
        }

    #endif

    #if (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12)

        /* Get a named variable from the frame's context.  Can raise if the variable is
        not present in the frame. */
        [[nodiscard]] Object get(const Str& name) const {
            PyObject* result = PyFrame_GetVar(self(), name.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(result);
        }

    #endif

};


template <>
struct __init__<Frame>                                      : Returns<Frame> {
    static auto operator()() {
        PyFrameObject* frame = PyEval_GetFrame();
        if (frame == nullptr) {
            throw RuntimeError("no frame is currently executing");
        }
        return reinterpret_borrow<Frame>((PyObject*)frame);
    }
};


template <std::integral T>
struct __init__<Frame, T>                                    : Returns<Frame> {
    static auto operator()(int skip) {
        // TODO: negative indexing should count from least recent frame?
        if (skip < 0) {
            throw ValueError("frame index cannot be negative");
        }
        PyFrameObject* frame = (PyFrameObject*)Py_XNewRef(PyEval_GetFrame());
        if (frame == nullptr) {
            throw RuntimeError("no frame is currently executing");
        }
        PyFrameObject* result = frame;
        for (int i = 0; i < skip; ++i) {
            PyFrameObject* temp = PyFrame_GetBack(result);
            Py_DECREF(result);
            if (temp == nullptr) {
                throw IndexError("frame index out of range");
            }
            result = temp;
        }
        return reinterpret_steal<Frame>((PyObject*)result);
    }
};


template <>
struct __init__<Frame, cpptrace::stacktrace_frame, PyThreadState*> : Returns<Frame> {
    static auto operator()(
        const cpptrace::stacktrace_frame& frame,
        PyThreadState* thread_state
    ) {
        return reinterpret_steal<Frame>(
            reinterpret_cast<PyObject*>(impl::StackFrame(
                frame,
                thread_state
            ).to_python())
        );
    }
};
template <>
struct __init__<Frame, cpptrace::stacktrace_frame>          : Returns<Frame> {
    static auto operator()(const cpptrace::stacktrace_frame& frame) {
        return reinterpret_steal<Frame>(
            reinterpret_cast<PyObject*>(impl::StackFrame(
                frame,
                nullptr
            ).to_python())
        );
    }
};


template <
    std::convertible_to<const char*> File,
    std::convertible_to<const char*> Func,
    std::convertible_to<int> Line
>
struct __init__<Frame, File, Func, Line, PyThreadState*> : Returns<Frame> {
    static auto operator()(
        const char* filename,
        const char* funcname,
        int lineno,
        PyThreadState* thread_state
    ) {
        return reinterpret_steal<Frame>(
            reinterpret_cast<PyObject*>(impl::StackFrame(
                filename,
                funcname,
                lineno,
                false,
                thread_state
            ).to_python())
        );
    }
};
template <
    std::convertible_to<const char*> File,
    std::convertible_to<const char*> Func,
    std::convertible_to<int> Line
>
struct __init__<Frame, File, Func, Line> : Returns<Frame> {
    static auto operator()(const char* filename, const char* funcname, int lineno) {
        return reinterpret_steal<Frame>(
            reinterpret_cast<PyObject*>(impl::StackFrame(
                filename,
                funcname,
                lineno,
                false,
                nullptr
            ).to_python())
        );
    }
};


}  // namespace py


#endif
