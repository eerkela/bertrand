#if !defined(BERTRAND_PYTHON_INCLUDED) && !defined(LINTER)
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_FUNC_H
#define BERTRAND_PYTHON_FUNC_H

#include <fstream>

#include "common.h"
#include "dict.h"
#include "str.h"
#include "tuple.h"
#include "list.h"
#include "type.h"


namespace bertrand {
namespace py {


////////////////////
////    CODE    ////
////////////////////


template <std::derived_from<Code> T>
struct __getattr__<T, "co_name">                                : Returns<Str> {};
template <std::derived_from<Code> T>
struct __getattr__<T, "co_qualname">                            : Returns<Str> {};
template <std::derived_from<Code> T>
struct __getattr__<T, "co_argcount">                            : Returns<Int> {};
template <std::derived_from<Code> T>
struct __getattr__<T, "co_posonlyargcount">                     : Returns<Int> {};
template <std::derived_from<Code> T>
struct __getattr__<T, "co_kwonlyargcount">                      : Returns<Int> {};
template <std::derived_from<Code> T>
struct __getattr__<T, "co_nlocals">                             : Returns<Int> {};
template <std::derived_from<Code> T>
struct __getattr__<T, "co_varnames">                            : Returns<Tuple<Str>> {};
template <std::derived_from<Code> T>
struct __getattr__<T, "co_cellvars">                            : Returns<Tuple<Str>> {};
template <std::derived_from<Code> T>
struct __getattr__<T, "co_freevars">                            : Returns<Tuple<Str>> {};
template <std::derived_from<Code> T>
struct __getattr__<T, "co_code">                                : Returns<Bytes> {};
template <std::derived_from<Code> T>
struct __getattr__<T, "co_consts">                              : Returns<Tuple<Object>> {};
template <std::derived_from<Code> T>
struct __getattr__<T, "co_names">                               : Returns<Tuple<Str>> {};
template <std::derived_from<Code> T>
struct __getattr__<T, "co_filename">                            : Returns<Str> {};
template <std::derived_from<Code> T>
struct __getattr__<T, "co_firstlineno">                         : Returns<Int> {};
template <std::derived_from<Code> T>
struct __getattr__<T, "co_stacksize">                           : Returns<Int> {};
template <std::derived_from<Code> T>
struct __getattr__<T, "co_flags">                               : Returns<Int> {};
template <std::derived_from<Code> T>
struct __getattr__<T, "co_positions">                           : Returns<Function> {};
template <std::derived_from<Code> T>
struct __getattr__<T, "co_lines">                               : Returns<Function> {};
template <std::derived_from<Code> T>
struct __getattr__<T, "replace">                                : Returns<Function> {};


/* A new subclass of pybind11::object that represents a compiled Python code object,
enabling seamless embedding of Python as a scripting language within C++.

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

    inline PyCodeObject* self() const {
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

    template <typename T>
    static consteval bool check() {
        return std::derived_from<T, Code>;
    }

    template <typename T>
    static constexpr bool check(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return check<T>();
        } else if constexpr (check<T>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::python_like<T>) {
            return obj.ptr() != nullptr && PyCode_Check(obj.ptr());
        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    Code(Handle h, const borrowed_t& t) : Base(h, t) {}
    Code(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (check<T>())
    Code(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    Code(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Code>(accessor).release(), stolen_t{})
    {}

    /* Compile a Python source file into an interactive code object. */
    explicit Code(const char* path) : Base(load(path), stolen_t{}) {}

    /* Compile a Python source file into an interactive code object. */
    explicit Code(const std::string& path) : Code(path.c_str()) {}

    /* Compile a Python source file into an interactive code object. */
    explicit Code(const std::string_view& path) : Code(path.data()) {}

    /* Parse and compile a source string into a Python code object. */
    static Code compile(const char* source) {
        return reinterpret_steal<Code>(build(source));
    }

    /* Parse and compile a source string into a Python code object. */
    static Code compile(const std::string& source) {
        return reinterpret_steal<Code>(build(source));
    }

    /* Parse and compile a source string into a Python code object. */
    static Code compile(const std::string_view& path) {
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
        return std::move(context);
    }

    /////////////////////
    ////    SLOTS    ////
    /////////////////////

    /* Get the name of the file from which the code was compiled. */
    inline Str filename() const {
        return reinterpret_borrow<Str>(self()->co_filename);
    }

    /* Get the function's base name. */
    inline Str name() const {
        return reinterpret_borrow<Str>(self()->co_name);
    }

    /* Get the function's qualified name. */
    inline Str qualname() const {
        return reinterpret_borrow<Str>(self()->co_qualname);
    }

    /* Get the first line number of the function. */
    inline Py_ssize_t line_number() const noexcept {
        return self()->co_firstlineno;
    }

    /* Get the total number of positional arguments for the function, including
    positional-only arguments and those with default values (but not variable
    or keyword-only arguments). */
    inline Py_ssize_t argcount() const noexcept {
        return self()->co_argcount;
    }

    /* Get the number of positional-only arguments for the function, including
    those with default values.  Does not include variable positional or keyword
    arguments. */
    inline Py_ssize_t posonlyargcount() const noexcept {
        return self()->co_posonlyargcount;
    }

    /* Get the number of keyword-only arguments for the function, including those
    with default values.  Does not include positional-only or variable
    positional/keyword arguments. */
    inline Py_ssize_t kwonlyargcount() const noexcept {
        return self()->co_kwonlyargcount;
    }

    /* Get the number of local variables used by the function (including all
    parameters). */
    inline Py_ssize_t nlocals() const noexcept {
        return self()->co_nlocals;
    }

    /* Get a tuple containing the names of the local variables in the function,
    starting with parameter names. */
    inline Tuple<Str> varnames() const {
        return attr<"co_varnames">();
    }

    /* Get a tuple containing the names of local variables that are referenced by
    nested functions within this function (i.e. those that are stored in a
    PyCell). */
    inline Tuple<Str> cellvars() const {
        return attr<"co_cellvars">();
    }

    /* Get a tuple containing the names of free variables in the function (i.e.
    those that are not stored in a PyCell). */
    inline Tuple<Str> freevars() const {
        return attr<"co_freevars">();
    }

    /* Get the required stack space for the code object. */
    inline Py_ssize_t stacksize() const noexcept {
        return self()->co_stacksize;
    }

    /* Get the bytecode buffer representing the sequence of instructions in the
    function. */
    inline Bytes bytecode() const;  // defined in func.h

    /* Get a tuple containing the literals used by the bytecode in the function. */
    inline Tuple<Object> consts() const {
        return reinterpret_borrow<Tuple<Object>>(self()->co_consts);
    }

    /* Get a tuple containing the names used by the bytecode in the function. */
    inline Tuple<Str> names() const {
        return reinterpret_borrow<Tuple<Str>>(self()->co_names);
    }

    /* Get an integer encoding flags for the Python interpreter. */
    inline int flags() const noexcept {
        return self()->co_flags;
    }

};


/////////////////////
////    FRAME    ////
/////////////////////


template <std::derived_from<Frame> T>
struct __getattr__<T, "f_back">                                 : Returns<Frame> {};
template <std::derived_from<Frame> T>
struct __getattr__<T, "f_code">                                 : Returns<Code> {};
template <std::derived_from<Frame> T>
struct __getattr__<T, "f_locals">                               : Returns<Dict<Str, Object>> {};
template <std::derived_from<Frame> T>
struct __getattr__<T, "f_globals">                              : Returns<Dict<Str, Object>> {};
template <std::derived_from<Frame> T>
struct __getattr__<T, "f_builtins">                             : Returns<Dict<Str, Object>> {};
template <std::derived_from<Frame> T>
struct __getattr__<T, "f_lasti">                                : Returns<Int> {};
template <std::derived_from<Frame> T>
struct __getattr__<T, "f_trace">                                : Returns<Function> {};
template <std::derived_from<Frame> T>
struct __setattr__<T, "f_trace", Function>                      : Returns<void> {};
template <std::derived_from<Frame> T>
struct __delattr__<T, "f_trace">                                : Returns<void> {};
template <std::derived_from<Frame> T>
struct __getattr__<T, "f_trace_lines">                          : Returns<Bool> {};
template <std::derived_from<Frame> T>
struct __setattr__<T, "f_trace_lines", Bool>                    : Returns<void> {};
template <std::derived_from<Frame> T>
struct __getattr__<T, "f_trace_opcodes">                        : Returns<Bool> {};
template <std::derived_from<Frame> T>
struct __setattr__<T, "f_trace_opcodes", Bool>                  : Returns<void> {};
template <std::derived_from<Frame> T>
struct __getattr__<T, "f_lineno">                               : Returns<Bool> {};
template <std::derived_from<Frame> T>
struct __setattr__<T, "f_lineno", Int>                          : Returns<void> {};
template <std::derived_from<Frame> T>
struct __getattr__<T, "clear">                                  : Returns<Function> {};


/* Represents a statically-typed Python frame object in C++.  These are the same frames
returned by the `inspect` module and listed in exception tracebacks.  They can be used
to run Python code in an interactive loop via the embedded code object. */
class Frame : public Object {
    using Base = Object;

    inline PyFrameObject* self() const {
        return reinterpret_cast<PyFrameObject*>(this->ptr());
    }

public:
    static const Type type;

    template <typename T>
    static consteval bool check() {
        return std::derived_from<T, Frame>;
    }

    template <typename T>
    static constexpr bool check(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return check<T>();
        } else if constexpr (check<T>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && PyFrame_Check(obj.ptr());
        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    Frame(Handle h, const borrowed_t& t) : Base(h, t) {}
    Frame(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (check<T>())
    Frame(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    Frame(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Frame>(accessor).release(), stolen_t{})
    {}

    /* Default constructor.  Initializes to the current execution frame. */
    Frame() : Base(reinterpret_cast<PyObject*>(PyEval_GetFrame()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw RuntimeError("no frame is currently executing");
        }
    }

    /* Construct an empty frame from a function name, file name, and line number.  This
    is primarily used to represent C++ contexts in Python exception tracebacks, etc. */
    Frame(
        const char* funcname,
        const char* filename,
        int lineno,
        PyThreadState* thread_state = nullptr
    ) : Base(
        reinterpret_cast<PyObject*>(impl::StackFrame(
            funcname,
            filename,
            lineno,
            false,
            thread_state
        ).to_python()),
        borrowed_t{}
    ) {}

    /* Construct an empty frame from a cpptrace::stacktrace_frame object. */
    Frame(
        const cpptrace::stacktrace_frame& frame,
        PyThreadState* thread_state = nullptr
    ) : Base(
        reinterpret_cast<PyObject*>(impl::StackFrame(
            frame,
            thread_state
        ).to_python()),
        borrowed_t{}
    ) {}

    /* Skip backward a number of frames on construction. */
    explicit Frame(size_t skip) :
        Base(reinterpret_cast<PyObject*>(PyEval_GetFrame()), stolen_t{})
    {
        if (m_ptr == nullptr) {
            throw RuntimeError("no frame is currently executing");
        }
        for (size_t i = 0; i < skip; ++i) {
            m_ptr = reinterpret_cast<PyObject*>(PyFrame_GetBack(self()));
            if (m_ptr == nullptr) {
                throw IndexError("frame index out of range");
            }
        }
    }

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Get the next outer frame from this one. */
    inline Frame back() const {
        PyFrameObject* result = PyFrame_GetBack(self());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Frame>(reinterpret_cast<PyObject*>(result));
    }

    /* Get the code object associated with this frame. */
    inline Code code() const {
        return reinterpret_steal<Code>(
            reinterpret_cast<PyObject*>(PyFrame_GetCode(self()))  // never null
        );
    }

    /* Get the line number that the frame is currently executing. */
    inline int line_number() const noexcept {
        return PyFrame_GetLineNumber(self());
    }

    /* Execute the code object stored within the frame using its current context.  This
    is the main entry point for the Python interpreter, and is used behind the scenes
    whenever a program is run. */
    inline Object operator()() const {
        PyObject* result = PyEval_EvalFrame(self());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_steal<Object>(result);
    }

    #if (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 11)

        /* Get the frame's builtin namespace. */
        inline Dict<Str, Object> builtins() const {
            return reinterpret_steal<Dict<Str, Object>>(
                PyFrame_GetBuiltins(self())
            );
        }

        /* Get the frame's globals namespace. */
        inline Dict<Str, Object> globals() const {
            PyObject* result = PyFrame_GetGlobals(self());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Dict<Str, Object>>(result);
        }

        /* Get the frame's locals namespace. */
        inline Dict<Str, Object> locals() const {
            PyObject* result = PyFrame_GetLocals(self());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Dict<Str, Object>>(result);
        }

        /* Get the generator, coroutine, or async generator that owns this frame, or
        nullopt if this frame is not owned by a generator. */
        inline std::optional<Object> generator() const {
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
        inline int last_instruction() const noexcept {
            return PyFrame_GetLasti(self());
        }

    #endif

    #if (PY_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12)

        /* Get a named variable from the frame's context.  Can raise if the variable is
        not present in the frame. */
        inline Object get(const Str& name) const {
            PyObject* result = PyFrame_GetVar(self(), name.ptr());
            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(result);
        }

    #endif

};


////////////////////////////////////
////    ARGUMENT ANNOTATIONS    ////
////////////////////////////////////


namespace impl {

    struct ArgTag {};
    struct OptionalArgTag {};
    struct VariadicPositionalTag {};
    struct VariadicKeywordTag {};

    template <StaticStr Name, typename T>
    struct OptionalArg;

}


/* A compile-time argument annotation that represents a bound positional or keyword
argument to a py::Function. */
template <StaticStr Name, typename T>
struct Arg : public impl::ArgTag {
    static constexpr StaticStr name = Name;
    using type = T;
    using optional = impl::OptionalArg<Name, T>;

    T value;

    Arg(T&& value) : value(std::forward<T>(value)) {}
    Arg(const Arg<Name, T>& other) : value(other.value) {}
    template <std::convertible_to<T> V>
    Arg(const Arg<Name, V>& other) : value(other.value) {}
    Arg(Arg<Name, T>&& other) : value(std::move(other.value)) {}
    template <std::convertible_to<T> V>
    Arg(Arg<Name, V>&& other) : value(std::move(other.value)) {}

    const T& operator*() const { return value; }
    const std::remove_reference_t<T>* operator->() const { return &value; }

    operator T&() & { return value; }
    operator T&&() && { return std::move(value); }
    operator const std::remove_const_t<T>&() const & { return value; }
    operator const std::remove_const_t<T>&&() const && { return value; }
};


/* A compile-time argument annotation that represents variadic positional arguments to
a py::Function. */
template <typename T>
struct Args : public impl::VariadicPositionalTag {
    using type = T;

    std::vector<T> value;

    Args(const std::vector<T>& value) : value(value) {}
    Args(std::vector<T>&& value) : value(std::move(value)) {}
    Args(const Args<T>& other) : value(other.value) {}
    template <std::convertible_to<T> V>
    Args(const Args<V>& other) : value(other.value) {}
    Args(Args<T>&& other) : value(std::move(other.value)) {}
    template <std::convertible_to<T> V>
    Args(Args<V>&& other) : value(std::move(other.value)) {}

    const std::vector<T>& operator*() const { return value; }
    const std::vector<T>* operator->() const { return &value; }

    operator std::vector<T>&() & { return value; }
    operator std::vector<T>&&() && { return std::move(value); }
    operator const std::vector<T>&() const & { return value; }
    operator const std::vector<T>&&() const && { return value; }
};


/* A compile-time argument annotation that represents variadic keyword arguments to a
py::Function. */
template <typename T>
struct Kwargs : public impl::VariadicKeywordTag {
    using type = T;

    std::unordered_map<std::string, T> value;  // TODO: string_view?

    Kwargs(const std::unordered_map<std::string, T>& value) : value(value) {}
    Kwargs(std::unordered_map<std::string, T>&& value) : value(std::move(value)) {}
    Kwargs(const Kwargs<T>& other) : value(other.value) {}
    template <std::convertible_to<T> V>
    Kwargs(const Kwargs<V>& other) : value(other.value) {}
    Kwargs(Kwargs<T>&& other) : value(std::move(other.value)) {}
    template <std::convertible_to<T> V>
    Kwargs(Kwargs<V>&& other) : value(std::move(other.value)) {}

    const std::unordered_map<std::string, T>& operator*() const { return value; }
    const std::unordered_map<std::string, T>* operator->() const { return &value; }

    operator std::unordered_map<std::string, T>&() & { return value; }
    operator std::unordered_map<std::string, T>&&() && { return std::move(value); }
    operator const std::unordered_map<std::string, T>&() const & { return value; }
    operator const std::unordered_map<std::string, T>&&() const && { return value; }
};


namespace impl {

    /* An annotated argument subclass that marks the argument as being optional when
    the function is called.  A matching default must be supplied to the constructor
    when the function is initialized. */
    template <StaticStr Name, typename T>
    struct OptionalArg : public Arg<Name, T>, public OptionalArgTag {
        OptionalArg(T&& value) : Arg<Name, T>(std::forward<T>(value)) {}
        OptionalArg(const Arg<Name, T>& other) : Arg<Name, T>(other) {}
        template <std::convertible_to<T> V>
        OptionalArg(const Arg<Name, V>& other) : Arg<Name, T>(other) {}
        OptionalArg(Arg<Name, T>&& other) : Arg<Name, T>(std::move(other)) {}
        template <std::convertible_to<T> V>
        OptionalArg(Arg<Name, V>&& other) : Arg<Name, T>(std::move(other)) {}
    };

    /* A compile-time tag that allows for the familiar `py::arg<"name"> = value`
    syntax.  The `py::arg<"name">` bit resolves to an instance of this class, and the
    argument becomes bound when the `=` operator is applied to it. */
    template <StaticStr name>
    struct UnboundArg {
        template <typename T>
        constexpr Arg<name, std::decay_t<T>> operator=(T&& value) const {
            return {std::forward<T>(value)};
        }
    };

    /* Introspect the proper signature for a py::Function instance from a generic
    function pointer, reference, or object, such as a lambda. */
    template <typename T>
    struct GetSignature;

    template <typename R, typename... A>
    struct GetSignature<R(*)(A...)> {
        using type = R(A...);
    };

    template <typename R, typename... A>
    struct GetSignature<R(*)(A...) noexcept> {
        using type = R(A...);
    };

    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...)> {
        using type = R(A...);
    };

    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) noexcept> {
        using type = R(A...);
    };

    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) const> {
        using type = R(A...);
    };

    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) const noexcept> {
        using type = R(A...);
    };

    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) volatile> {
        using type = R(A...);
    };

    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) volatile noexcept> {
        using type = R(A...);
    };

    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) const volatile> {
        using type = R(A...);
    };

    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) const volatile noexcept> {
        using type = R(A...);
    };

    template <impl::has_call_operator T>
    struct GetSignature<T> {
        using type = GetSignature<decltype(&T::operator())>::type;
    };

}


/* A universal function wrapper that can represent either a Python function exposed to
C++, or a C++ function exposed to Python with equivalent semantics.

This class contains 2 complementary components depending on how it was constructed:

    1.  A Python function object, which can be automatically generated from a C++
        function pointer, lambda, or object with an overloaded call operator.  If
        it was automatically generated, it will appear to Python as a
        `builtin_function_or_method` that forwards to the C++ function when called.

    2.  A C++ `std::function` that holds the C++ implementation in a type-erased
        form.  This is directly called when the function is invoked from C++, and it
        can be automatically generated from a Python function using the ::borrow() and
        ::steal() constructors.  If it was automatically generated, it will delegate to
        the Python function through the C API.

The combination of these two components allows the function to be passed and called
with identical semantics in both languages, including automatic type conversions, error
handling, keyword arguments, default values, and container unpacking, all of which is
resolved statically at compile time by introspecting the underlying signature (which
can be deduced using CTAD).  In order to facilitate this, bertrand provides a
lightweight `py::Arg` annotation, which can be placed directly in the function
signature to enable these features without impacting performance or type safety.

For instance, consider the following function:

    int subtract(int x, int y) {
        return x - y;
    }

We can directly wrap this as a `py::Function` if we want, which does not alter the
calling convention or signature in any way:

    py::Function func("subtract", subtract);
    func(1, 2);  // returns -1

If this function is exported to Python, it's call signature will remain unchanged,
meaning that both arguments must be supplied as positional-only arguments, and no
default values will be considered.

    static const py::Code script = R"(
        func(1, 2)  # ok
        # func(1)  # error: missing required positional argument
        # func(x = 1, y = 2)  # error: unexpected keyword argument
    )"_python;

    script({{"func", func}});  // prints -1

We can add parameter names and default values by annotating the C++ function (or a
wrapper around it) with `py::Arg` tags.  For instance:

    auto wrapper = [](py::Arg<"x", int> x, py::Arg<"y", int>::optional y = 2) {
        return subtract(x, y);
    };

    py::Function func("subtract", wrapper, py::arg<"y"> = 2);

Note that the annotations themselves are implicitly convertible to the underlying
argument types, so they should be acceptable as inputs to most functions without any
additional syntax.  If necessary, they can be explicitly dereferenced through the `*`
and `->` operators, or by accessing their `.value` member directly, which comprises
their entire interface.  Also note that for each argument marked as `::optional`, we
must provide a default value within the function's constructor, which will be
substituted whenever we call the function without specifying that argument.

With this in place, we can now do the following:

    func(1);
    func(1, 2);
    func(1, py::arg<"y"> = 2);

    // or, equivalently:
    static constexpr auto x = py::arg<"x">;
    static constexpr auto y = py::arg<"y">;

    func(x = 1);
    func(x = 1, y = 2);
    func(y = 2, x = 1);  // keyword arguments can have arbitrary order

All of which will return the same result as before.  The function can also be passed to
Python and called with the same semantics:

    static const py::Code script = R"(
        func(1)
        func(1, 2)
        func(1, y = 2)
        func(x = 1)
        func(x = 1, y = 2)
        func(y = 2, x = 1)
    )"_python;

    script({{"func", func}});

What's more, all of the logic necessary to handle these cases is resolved statically at
compile time, meaning that there is no runtime overhead for using these annotations,
and no additional code is generated for the function itself.  When it is called from
C++, all we have to do is inspect the provided arguments and match them against the
underlying signature, which generates a compile time index sequence that can be used to
reorder the arguments and insert default values as needed.  In fact, each of the above
invocations will be transformed into the same underlying function call, with the same
performance characteristics as the original function (disregarding any overhead from
the `std::function` wrapper itself).

Additionally, since all arguments are evaluated purely at compile time, we can enforce
strong type safety guarantees on the function signature and disallow invalid calls
using a template constraint.  This means that proper call syntax is automatically
enforced throughout the codebase, in a way that allows static analyzers (like clangd)
to give proper syntax highlighting and LSP support.

Lastly, besides the `py::Arg` annotation, Bertrand also provides equivalent `py::Args`
and `py::Kwargs` tags, which represent variadic positional and keyword arguments,
respectively.  These will automatically capture an arbitrary number of arguments in
addition to those specified in the function signature itself, and will encapsulate them
in either a `std::vector<T>` or `std::unordered_map<std::string_view, T>`, respectively.
The allowable types can be specified by templating the annotation on the desired type,
to which all captured arguments must be convertible.

Similarly, Bertrand allows Pythonic unpacking of supported containers into the function
signature via the `*` and `**` operators, which emulate their Python equivalents.  Note
that in order to properly enable this, the `*` operator must be overloaded to return a
`py::Unpack` object or one of its subclasses, which is done automatically for any
iterable subclass of py::Object.  Additionally, unpacking a container like this carries
with it special restrictions, including the following:

    1.  The unpacked container must be the last argument in its respective category
        (positional or keyword), and there can only be at most one of each at the call
        site.  These are not reflected in ordinary Python, but are necessary to ensure
        that compile-time argument matching is unambiguous.
    2.  The container's value type must be convertible to each of the argument types
        that follow it in the function signature, or else a compile error will be
        raised.
    3.  If double unpacking is performed, then the container must yield key-value pairs
        where the key is implicitly convertible to a string, and the value is
        convertible to the corresponding argument type.  If this is not the case, a
        compile error will be raised.
    4.  If the container does not contain enough elements to satisfy the remaining
        arguments, or it contains too many, a runtime error will be raised when the
        function is called.  Because it is impossible to know the size of the container
        at compile time, this is the only way to enforce this constraint.
*/
template <typename F = Object(Args<Object>, Kwargs<Object>)>
class Function_ : public Function_<typename impl::GetSignature<F>::type> {};


template <typename Return, typename... Target>
class Function_<Return(Target...)> {
protected:

    ///////////////////////
    ////    HELPERS    ////
    ///////////////////////

    template <typename T>
    struct inspect {
        using type                                      = T;
        template <StaticStr name>
        static constexpr bool named                     = false;
        static constexpr bool optional                  = false;
        static constexpr bool positional_only           = true;
        static constexpr bool keyword                   = false;
        static constexpr bool variadic_positional       = false;
        static constexpr bool variadic_keyword          = false;
    };

    template <std::derived_from<impl::ArgTag> T>
    struct inspect<T> {
        using type                                      = std::decay_t<T>::type;
        template <StaticStr name>
        static constexpr bool named                     = std::decay_t<T>::name == name;
        static constexpr bool optional                  =
            std::derived_from<std::decay_t<T>, impl::OptionalArgTag>;
        static constexpr bool positional_only           = false;
        static constexpr bool keyword                   = true;
        static constexpr bool variadic_positional       = false;
        static constexpr bool variadic_keyword          = false;
    };

    template <std::derived_from<impl::VariadicPositionalTag> T>
    struct inspect<T> {
        using type                                      = std::decay_t<T>::type;
        template <StaticStr name>
        static constexpr bool named                     = false;
        static constexpr bool optional                  = false;
        static constexpr bool positional_only           = false;
        static constexpr bool keyword                   = false;
        static constexpr bool variadic_positional       = true;
        static constexpr bool variadic_keyword          = false;
    };

    template <std::derived_from<impl::VariadicKeywordTag> T>
    struct inspect<T> {
        using type                                      = std::decay_t<T>::type;
        template <StaticStr name>
        static constexpr bool named                     = false;
        static constexpr bool optional                  = false;
        static constexpr bool positional_only           = false;
        static constexpr bool keyword                   = false;
        static constexpr bool variadic_positional       = false;
        static constexpr bool variadic_keyword          = true;
    };

    /* Retrieve the type at index I of the given parameter pack. */
    template <size_t I, typename... Ts>
    using type = std::tuple_element<I, std::tuple<Ts...>>::type;

    /* Find the index of the given keyword in the paremeter pack, or its total size if
    the keyword is not present. */
    template <StaticStr name, typename... Ts>
    static constexpr size_t index = 0;
    template <StaticStr name, typename T, typename... Ts>
    static constexpr size_t index<name, T, Ts...> =
        inspect<std::decay_t<T>>::template named<name> ? 0 : 1 + index<name, Ts...>;

    /* Get the index of the first optional argument in the parameter pack, or its total
    size if no optional arguments are present. */
    template <typename... Ts>
    static constexpr size_t optional_index = 0;
    template <typename T, typename... Ts>
    static constexpr size_t optional_index<T, Ts...> =
        inspect<std::decay_t<T>>::optional ? 0 : 1 + optional_index<Ts...>;

    template <size_t I, typename... Ts>
    static constexpr size_t n_optional_recursive = I;
    template <size_t I, typename T, typename... Ts>
    static constexpr size_t n_optional_recursive<I, T, Ts...> = n_optional_recursive<
         I + std::derived_from<std::decay_t<T>,
         impl::OptionalArgTag>,
         Ts...
    >;

    /* Get the total number of optional arguments in the parameter pack. */
    template <typename... Ts>
    static constexpr size_t n_optional = n_optional_recursive<0, Ts...>;

    /* Get the index of the first keyword argument in the parameter pack, or its total
    size if no keywords are found. */
    template <typename... Ts>
    static constexpr size_t keyword_index = 0;
    template <typename T, typename... Ts>
    static constexpr size_t keyword_index<T, Ts...> =
        inspect<std::decay_t<T>>::keyword ? 0 : 1 + keyword_index<Ts...>;

    /* Get the index of the first variadic positional argument in the parameter pack,
    or its total size if variadic positional arguments are not allowed. */
    template <typename... Ts>
    static constexpr size_t args_index = 0;
    template <typename T, typename... Ts>
    static constexpr size_t args_index<T, Ts...> =
        inspect<std::decay_t<T>>::variadic_positional ? 0 : 1 + args_index<Ts...>;

    /* Get the index of the first variadic keyword argument in the parameter pack, or
    its total size if variadic keyword arguments are not allowed. */
    template <typename... Ts>
    static constexpr size_t kwargs_index = 0;
    template <typename T, typename... Ts>
    static constexpr size_t kwargs_index<T, Ts...> =
        inspect<std::decay_t<T>>::variadic_keyword ? 0 : 1 + kwargs_index<Ts...>;

    /* Index into a parameter pack to forward the associated argument. */
    template <size_t I>
    static void get_arg() {
        static_assert(false, "index out of range for parameter pack");
    }

    template <size_t I, typename T, typename... Ts>
    static decltype(auto) get_arg(T&& curr, Ts&&... next) {
        if constexpr (I == 0) {
            return std::forward<T>(curr);
        } else {
            return get_arg<I - 1>(std::forward<Ts>(next)...);
        }
    }

    /////////////////////////////////
    ////    STATIC ASSERTIONS    ////
    /////////////////////////////////

    template <size_t I, typename... Ts>
    static constexpr bool validate_target_signature = true;

    template <size_t I, typename T, typename... Ts>
    static constexpr bool validate_target_signature<I, T, Ts...> = [] {
        if constexpr (inspect<T>::positional_only) {
            static_assert(
                I < keyword_index<Target...>,
                "positional-only arguments must precede keywords"
            );
            static_assert(
                I < args_index<Target...>,
                "positional-only arguments must precede variadic positional arguments"
            );
            static_assert(
                I < kwargs_index<Target...>,
                "positional-only arguments must precede variadic keyword arguments"
            );
            static_assert(
                I < optional_index<Target...> || inspect<T>::optional,
                "all arguments after the first optional argument must also be optional"
            );

        } else if constexpr (inspect<T>::keyword) {
            static_assert(
                I < kwargs_index<Target...>,
                "keyword arguments must precede variadic keyword arguments"
            );
            static_assert(
                I == index<T::name, Target...>,
                "signature contains multiple keyword arguments with the same name"
            );
            static_assert(
                I < optional_index<Target...> || inspect<T>::optional,
                "all arguments after the first optional argument must also be optional"
            );

        } else if constexpr (inspect<T>::variadic_positional) {
            static_assert(
                I < kwargs_index<Target...>,
                "variadic positional arguments must precede variadic keyword arguments"
            );
            static_assert(
                I == args_index<Target...>,
                "signature contains multiple variadic positional arguments"
            );

        } else if constexpr (inspect<T>::variadic_keyword) {
            static_assert(
                I == kwargs_index<Target...>,
                "signature contains multiple variadic keyword arguments"
            );
        }

        return validate_target_signature<I + 1, Ts...>;
    }();

    /* If the target signature does not conform to Python calling conventions, throw
    an informative static assertion. */
    static_assert(validate_target_signature<0, std::decay_t<Target>...>);

    //////////////////////////////
    ////    DEFAULT VALUES    ////
    //////////////////////////////

    /* Represents a default value in the Defaults tuple.  Identifies each argument by
    its index in the target signature. */
    template <size_t I, StaticStr Name, typename T>
    struct DefaultValue  {
        static constexpr StaticStr name = Name;
        static constexpr size_t index = I;
        using type = std::decay_t<T>;
        type value;
    };

    template <size_t I, typename Tuple, typename... Ts>
    struct CollectDefaults {
        using type = Tuple;
    };

    template <size_t I, typename... Defaults, typename T, typename... Ts>
    struct CollectDefaults<I, std::tuple<Defaults...>, T, Ts...> {

        template <typename U>
        struct Wrap {
            using type = CollectDefaults<
                I + 1,
                std::tuple<Defaults...>,
                Ts...
            >::type;
        };

        template <std::derived_from<impl::OptionalArgTag> U>
        struct Wrap<U> {
            using type = CollectDefaults<
                I + 1,
                std::tuple<Defaults..., DefaultValue<I, U::name, typename U::type>>,
                Ts...
            >::type;
        };

        using type = Wrap<std::decay_t<T>>::type;
    };

    /* A tuple type that encapsulates the default values that are associated with this
    function, which must be set when the function is constructed. */
    using Defaults = CollectDefaults<0, std::tuple<>, Target...>::type;

    template <size_t I, typename... Ts>
    struct find_default_recursive;

    template <size_t I>
    struct find_default_recursive<I, std::tuple<>> {
        static constexpr size_t value = 0;
    };

    template <size_t I, typename T, typename... Ts>
    struct find_default_recursive<I, std::tuple<T, Ts...>> {
        static constexpr size_t value = 
            (I == std::decay_t<T>::index) ?
            0 : 1 + find_default_recursive<I, std::tuple<Ts...>>::value;
    };

    /* Find the index within the Defaults tuple for the target argument at index I. */
    template <size_t I>
    static constexpr size_t find_default = find_default_recursive<I, Defaults>::value;

    /* Get the default value associated with the target argument at index I. */
    template <size_t I>
    static decltype(auto) get_default(const Defaults& defaults) {
        return std::get<find_default<I>>(defaults).value;
    };

    /* Statically analyzes the arguments that are supplied to the function's
    constructor, so that they fully satisfy the default value annotations. */
    template <typename... Values>
    struct ParseDefaults {

        // TODO: not sure if I'm properly decaying types here, or in this whole class
        // more generally.

        /* Recursively check whether the default values conform to Python calling
        conventions (i.e. no positional arguments after a keyword, no duplicate
        keywords, etc.). */
        template <size_t I, typename... Ts>
        static constexpr bool validate_defaults = true;

        template <size_t I, typename T, typename... Ts>
        static constexpr bool validate_defaults<I, T, Ts...> = [] {
            if constexpr (inspect<T>::positional_only) {
                if constexpr (I >= keyword_index<Values...>) {
                    return false;
                } else if constexpr (I >= n_optional<Target...>) {
                    return false;
                }

            } else if constexpr (inspect<T>::keyword) {
                if constexpr (I != index<T::name, Values...>) {
                    return false;
                } else if constexpr (index<T::name, Target...> == sizeof...(Target)) {
                    return false;
                }

            } else {
                return false;
            }

            return validate_defaults<I + 1, Ts...>;
        }();

        /* Recursively check whether the default values fully satisfy the Defaults
        tuple, accounting for keyword arguments. */
        template <size_t I, typename... Ts>
        static constexpr bool match_defaults = true;

        template <size_t I, typename T, typename... Ts> requires (I >= sizeof...(Target))
        static constexpr bool match_defaults<I, T, Ts...> = true;

        template <size_t I, typename T, typename... Ts>
        static constexpr bool match_defaults<I, T, Ts...> = [] {
            using Arg = std::decay_t<type<I, Target...>>;

            if constexpr (!inspect<Arg>::optional) {
                return match_defaults<I + 1, T, Ts...>;

            } else {
                using D = std::tuple_element<find_default<I>, Defaults>::type;

                if constexpr (inspect<T>::positional_only) {
                    if constexpr (!std::convertible_to<T, typename D::type>) {
                        return false;
                    }

                } else if constexpr (inspect<T>::keyword) {
                    if constexpr (index<Arg::name, Values...> != sizeof...(Values)) {
                        using V = type<index<Arg::name, Values...>, Values...>;
                        if constexpr (!std::convertible_to<V, typename D::type>) {
                            return false;
                        }
                    }
                }

                return match_defaults<I + 1, Ts...>;
            }
        }();

        /* Constructor is only enabled if the default values are fully satisfied. */
        static constexpr bool enable =
            sizeof...(Values) == n_optional<Target...> &&
            validate_defaults<0, std::decay_t<Values>...> &&
            match_defaults<0, std::decay_t<Values>...>;

        template <size_t I>
        static decltype(auto) extract_helper(Values&&... values) {
            if constexpr (I < keyword_index<Values...>) {
                return get_arg<I>(std::forward<Values>(values)...);
            } else {
                using D = std::tuple_element<I, Defaults>::type;
                return get_arg<index<D::name, Values...>>(std::forward<Values>(values)...);
            }
        }

        template <size_t... Is>
        static Defaults extract(std::index_sequence<Is...>, Values&&... values) {
            return {{extract_helper<Is>(std::forward<Values>(values)...)}...};
        }

        /* Build the default values tuple from the provided arguments, reordering them
        as needed to account for keywords. */
        static constexpr Defaults build(Values&&... values) {
            return extract(
                std::make_index_sequence<sizeof...(Values)>{},
                std::forward<Values>(values)...
            );
        }

    };

    /////////////////////////////
    ////    CALL OPERATOR    ////
    /////////////////////////////

    /* Statically analyzes the arguments that are supplied at the function's call site,
    so that they can be reconciled with the target signature. */
    template <typename... Source>
    struct Arguments {

        /* Recursively check whether the source signature conforms to Python calling
        conventions (i.e. no positional arguments after a keyword, no duplicate
        keywords, no extra arguments, etc.). */
        template <size_t I, typename... Ts>
        static constexpr bool validate_source = true;

        template <size_t I, typename T, typename... Ts>
        static constexpr bool validate_source<I, T, Ts...> = [] {
            if constexpr (inspect<T>::positional_only) {
                if constexpr (I >= keyword_index<Source...>) {
                    return false;
                } else if constexpr (
                    args_index<Target...> == sizeof...(Target) &&
                    I >= sizeof...(Target)
                ) {
                    return false;
                }

            } else if constexpr (inspect<T>::keyword) {
                if constexpr (I != index<T::name, Source...>) {
                    return false;
                } else if constexpr (
                    kwargs_index<Target...> == sizeof...(Target) &&
                    index<T::name, Target...> == sizeof...(Target)
                ) {
                    return false;
                }

            } else {
                return false;
            }

            return validate_source<I + 1, Ts...>;
        }();

        /* Recursively check whether the source arguments fully satisfy the target
        signature, after accounting for default values. */
        template <size_t I, typename... Ts>
        static constexpr bool match_target = true;

        template <size_t I, typename T, typename... Ts>
        static constexpr bool match_target<I, T, Ts...> = [] {
            if constexpr (!inspect<T>::optional) {
                if constexpr (inspect<T>::positional_only) {
                    if constexpr (I >= keyword_index<Source...>) {
                        return false;
                    }

                } else if constexpr (inspect<T>::keyword) {
                    if constexpr (
                        I >= keyword_index<Source...> &&
                        index<T::name, Source...> == sizeof...(Source)
                    ) {
                        return false;
                    }
                }
            }

            return match_target<I + 1, Ts...>;
        }();

        /* Call operator is only enabled if source arguments are well-formed and match
        the target signature. */
        static constexpr bool enable =
            validate_source<0, std::decay_t<Source>...> &&
            match_target<0, std::decay_t<Target>...>;

    private:

        // TODO: these could maybe just use pure template recursion, rather than
        // requiring an index sequence.

        template <size_t I, typename Result>
        static void build_keywords(Result& result, Source&&... args) {
            using Arg = std::decay_t<type<I, Source...>>;
            if constexpr (index<Arg::name, Target...> == sizeof...(Source)) {
                result.emplace(Arg::name, get_arg<I>(std::forward<Source>(args)...));
            }
        }

        template <typename T, size_t... Is>
        static std::unordered_map<std::string, T> variadic_keyword_helper(
            std::index_sequence<Is...>,
            Source&&... args
        ) {
            std::unordered_map<std::string, T> result;
            (
                build_keywords<keyword_index<Source...> + Is>(
                    result,
                    std::forward<Source>(args)...
                ),
                ...
            );
            return result;
        }

        template <size_t I, typename T, size_t... Is>
        static std::vector<T> variadic_positional_helper(
            std::index_sequence<Is...>,
            Source&&... args
        ) {
            return {get_arg<I + Is>(std::forward<Source>(args)...)...};
        }

    public:

        /* Extract a positional-only argument from the source signature at the given
        index. */
        template <size_t I>
        static decltype(auto) positional_only(
            const Defaults& defaults,
            Source&&... args
        ) {
            if constexpr (I < keyword_index<Source...>) {
                return get_arg<I>(std::forward<Source>(args)...);
            } else {
                return get_default<I>(defaults);
            }
        }

        /* Extract a keyword-only argument from the source signature at the given
        index. */
        template <size_t I, StaticStr name>
        static decltype(auto) keyword_only(
            const Defaults& defaults,
            Source&&... args
        ) {
            if constexpr (index<name, Source...> < sizeof...(Source)) {
                return get_arg<index<name, Source...>>(std::forward<Source>(args)...);
            } else {
                return get_default<I>(defaults);
            }
        }

        /* Extract a positional-or-keyword argument from the source signature. */
        template <size_t I, StaticStr name>
        static decltype(auto) positional_or_keyword(
            const Defaults& defaults,
            Source&&... args
        ) {
            if constexpr (I < keyword_index<Source...>) {
                return get_arg<I>(std::forward<Source>(args)...);
            } else if constexpr (index<name, Source...> < sizeof...(Source)) {
                return get_arg<index<name, Source...>>(std::forward<Source>(args)...);
            } else {
                return get_default<I>(defaults);
            }
        }

        /* Extract variadic positional arguments from the source signature starting at
        the given index. */
        template <size_t I, typename T>
        static std::vector<T> variadic_positional(
            const Defaults& defaults,
            Source&&... args
        ) {
            if constexpr (I < keyword_index<Source...>) {
                return variadic_positional_helper<I, T>(
                    std::make_index_sequence<keyword_index<Source...> - I>{},
                    std::forward<Source>(args)...
                );
            } else {
                return {};
            }
        }

        /* Extract variadic keyword arguments from the source signature by comparing
        them against the target signature. */
        template <typename T>
        static std::unordered_map<std::string, T> variadic_keyword(
            const Defaults& defaults,
            Source&&... args
        ) {
            return variadic_keyword_helper<T>(
                std::make_index_sequence<sizeof...(Source) - keyword_index<Source...>>{},
                std::forward<Source>(args)...
            );
        }


        // TODO: eventually support unpacking operator in a similar way.  This would
        // basically be the inverse of the variadic positional and keyword arguments,
        // except that I won't be able to determine the number of arguments at compile
        // time.  That would have to be done at runtime, or not at all.  This would be
        // really hard to do.

        // It might be possible to just disable missing argument errors if a container
        // unpacking operator is present, but that could result in some inconsistent
        // behavior.  First of all, it would only work for Python functions, since we're
        // building dynamic arguments anyways, and secondly, it would force the
        // unpacking operator to be the last in its respective positional/keyword list.
        // This is because we can't determine the number of arguments to unpack at
        // compile time, so we can't apply the same level of validation that we do for
        // the other arguments.

        // Perhaps unpacking is handled entirely through `py::Struct`?  Single unpacking
        // would expand it into positional arguments (the number of which is known at
        // compile time), and double unpacking would expand it into keyword arguments,
        // where the names are taken from the field names of the struct.
        // Also, when doing the inverse and building an Args/Kwargs pack, I could encode
        // them as a Struct, where the types are known at compile time, and may include
        // the respective names.

        // Of course, in practice I will be wrapping a PyCFunction, so I'll have to
        // build dynamic arguments no matter what.  That means I should theoretically
        // be able to support unpacking operators at the cost of some type safety
        // and the above restrictions on ordering, etc.

        // Maybe unpacking operators are only allowed to match an Args or Kwargs pack,
        // possibly along with other arguments that can be accumulated.

        // In fact, when building the final arguments, I'm going to have to filter
        // out nullopts anyways when building the resulting argument list.  Also, I
        // might want to do that myself, without going through cpp_function.  I can
        // build the argument list myself, and then call the function directly.

        // -> It just boils down to building the args/kwargs and then calling
        // PyObject_Call, which should always work since the argument types will be
        // restricted to Python objects, and implicit constructors will be invoked
        // as needed.

        // For signatures that contain parameter packs, I have to fall back to pybind11,
        // which has implemented the necessary logic to handle them.  I can't/won't
        // do that myself.  You'll just have to live with runtime errors in that case.

    };

    template <size_t I, typename... Source>
    static decltype(auto) extract_arg(const Defaults& defaults, Source&&... args) {
        using T = std::decay_t<type<I, Target...>>;
        using Args = Arguments<Source...>;

        if constexpr (inspect<T>::positional_only) {
            return Args::template positional_only<I>(
                defaults,
                std::forward<Source>(args)...
            );

        } else if constexpr (inspect<T>::keyword) {
            return Args::template positional_or_keyword<I, T::name>(
                defaults,
                std::forward<Source>(args)...
            );

        } else if constexpr (inspect<T>::variadic_positional) {
            return Args::template variadic_positional<I, typename T::type>(
                defaults,
                std::forward<Source>(args)...
            );

        } else if constexpr (inspect<T>::variadic_keyword) {
            return Args::template variadic_keyword<typename T::type>(
                defaults,
                std::forward<Source>(args)...
            );

        } else {
            static_assert(false, "unreachable");
        }
    };

    /* Implements the C++ call operator. */
    template <size_t... Is, typename... Source>
    Return call(std::index_sequence<Is...>, Source&&... args) const {
        return contents->func(
            extract_arg<Is>(
                contents->defaults,
                std::forward<Source>(args)...
            )...
        );
    }

    ///////////////////////////////
    ////    PYTHON BINDINGS    ////
    ///////////////////////////////

    /* A heap-allocated data structure that holds the core members of the function
    object.  A pointer to this object is stored in both the `py::Function` instance and
    a special `PyCapsule` that is passed up to the equivalent CPython wrapper.

    The lifetime of the `CapsuleContents` is complicated, and depends on how the
    `py::Function` was created.  If it was generated from a C++ function, then a
    corresponding `PyCFunction` wrapper will be created, which forwards to the C++
    function and makes it callable from Python.  In order for this to work, the
    `CapsuleContents` must remain valid for the lifetime of the `PyCFunction` wrapper,
    which can potentially outlive the `py::Function` that created it.  This means we
    have to heap-allocate the `CapsuleContents` and manage its lifetime from the
    CPython wrapper.  Since `py::Function` holds a strong reference to this wrapper,
    the `CapsuleContents` will always outlive any referencing `py::Function` instances.

    Conversely, if the `py::Function` was generated from a Python function, then the
    `PyCFunction` wrapper is not created, and the `CapsuleContents` is owned by the
    `py::Function` itself. */
    struct CapsuleContents {
        std::string name;
        std::function<Return(Target...)> func;
        Defaults defaults;
        PyMethodDef method_def;

        CapsuleContents(
            std::string func_name,
            std::function<Return(Target...)> func,
            Defaults defaults
        );

    };

    /* PyCapsule deleter that cleans up the `CapsuleContents` when the PyCFunction
    wrapper is garbage collected. */
    static void capsule_deleter(PyObject* capsule) {
        auto contents = reinterpret_cast<CapsuleContents*>(
            PyCapsule_GetPointer(capsule, nullptr)
        );
        delete contents;
    }

    /* Build a PyCFunction wrapper around the given function object. */
    static PyObject* wrap_python(CapsuleContents* contents) {
        PyObject* capsule = PyCapsule_New(
            contents,
            nullptr,
            &capsule_deleter
        );
        if (capsule == nullptr) {
            delete contents;
            Exception::from_python();
        }

        PyObject* result = PyCFunction_New(
            &contents->method_def,
            capsule
        );
        Py_DECREF(capsule);  // PyCFunction owns the only reference to the capsule
        if (result == nullptr) {
            Exception::from_python();
        }
        return result;
    }

    /* Get the `CapsuleContents` stored within a Python capsule, which replaces the
    `self` argument to the function wrapper. */
    static auto get_contents(PyObject* capsule) {
        auto result = reinterpret_cast<CapsuleContents*>(
            PyCapsule_GetPointer(capsule, nullptr)
        );
        if (result == nullptr) {
            Exception::from_python();
        }
        return result;
    }

    enum class CallPolicy {
        no_args,
        one_arg,
        positional,
        keyword
    };

    /* Choose an optimized Python call protocol based on the target signature. */
    static constexpr CallPolicy call_policy = [] {
        if constexpr (sizeof...(Target) == 0) {
            return CallPolicy::no_args;
        } else if constexpr (
            sizeof...(Target) == 1 &&
            inspect<std::decay_t<type<0, Target...>>>::positional_only
        ) {
            return CallPolicy::one_arg;
        } else if constexpr (
            keyword_index<Target...> == sizeof...(Target) &&
            kwargs_index<Target...> == sizeof...(Target)
        ) {
            return CallPolicy::positional;
        } else {
            return CallPolicy::keyword;
        }
    }();

    // TODO: really, these implementations are pretty abominable at the moment.

    /* Defines Python/C++ wrappers that allow the function to be called equivalently
    from both languages. */
    template <CallPolicy policy, typename Dummy = void>
    struct Def;

    template <typename Dummy>
    struct Def<CallPolicy::no_args, Dummy> {
        static constexpr int flags = METH_NOARGS;

        static PyObject* python(PyObject* capsule, PyObject* /* unused */) {
            try {
                return Object(get_contents(capsule)->func()).release().ptr();
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static std::function<Return(Target...)> cpp(
            PyObject* func,
            const Defaults& defaults
        ) {
            return [func, &defaults]() {
                PyObject* result = PyObject_CallNoArgs(func);
                if (result == nullptr) {
                    Exception::from_python();
                }
                return static_cast<Return>(reinterpret_steal<Object>(result));
            };
        }

    };

    template <typename Dummy>
    struct Def<CallPolicy::one_arg, Dummy> {
        static constexpr int flags = METH_O;

        static PyObject* python(PyObject* capsule, PyObject* obj) {
            try {
                return Object(get_contents(capsule)->func(
                    reinterpret_borrow<Object>(obj)
                )).release().ptr();
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static std::function<Return(Target...)> cpp(
            PyObject* func,
            const Defaults& defaults
        ) {
            return [func, &defaults](Target... args) {
                PyObject* result = PyObject_CallOneArg(func, Object(args).ptr()...);
                if (result == nullptr) {
                    Exception::from_python();
                }
                return static_cast<Return>(reinterpret_steal<Object>(result));
            };
        }

    };

    template <typename Dummy>
    struct Def<CallPolicy::positional, Dummy> {
        static constexpr int flags = METH_FASTCALL;

        // TODO: these last two protocols need to validate the Python arguments and
        // account for extra ones, etc.

        template <size_t I>
        static Object python_arg(PyObject* const* args, Py_ssize_t nargs) {
            // TODO: do stuff here.  All arguments are guaranteed to be positional-only,
            // but may include *args.

            // if (I < nargs) {
            //     return reinterpret_borrow<Object>(args[I]);
            // } else {
            //     return Object(get_default<I>(defaults));
            // }
        }

        template <size_t... Is>
        static PyObject* call_python(
            std::index_sequence<Is...>,
            PyObject* capsule,
            PyObject* const* args,
            Py_ssize_t nargs
        ) {
            return Object(get_contents(capsule)->func(
                python_arg<Is>(args, nargs)...
            )).release().ptr();
        };

        static PyObject* python(
            PyObject* capsule,
            PyObject* const* args,
            Py_ssize_t nargs
        ) {
            try {
                return call_python(
                    std::make_index_sequence<sizeof...(Target)>{},
                    capsule,
                    args,
                    nargs
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        // TODO: This needs to build an array of PyObject* const values, and then call
        // the function with them.  Maybe I do this in two arrays, one for the Object
        // values and one that holds their PyObject* pointers.  That should be the
        // fastest way to do this.

        static std::function<Return(Target...)> cpp(
            PyObject* func,
            const Defaults& defaults
        ) {
            return [func, &defaults](Target... args) {
                PyObject* result = PyObject_VectorCall(func, Object(args).ptr()...);  // incorrect
                if (result == nullptr) {
                    Exception::from_python();
                }
                return static_cast<Return>(reinterpret_steal<Object>(result));
            };
        }

    };

    template <typename Dummy>
    struct Def<CallPolicy::keyword, Dummy> {
        static constexpr int flags = METH_FASTCALL | METH_KEYWORDS;

        template <size_t I>
        static Object python_arg(
            PyObject* const* args,
            Py_ssize_t nargs,
            PyObject* kwnames
        ) {
            // TODO: do complicated stuff here
        }

        template <size_t... Is>
        static PyObject* call_python(
            std::index_sequence<Is...>,
            PyObject* capsule,
            PyObject* const* args,
            Py_ssize_t nargs,
            PyObject* kwnames
        ) {
            return Object(get_contents(capsule)->func(
                python_arg<Is>(args, nargs, kwnames)...
            )).release().ptr();
        }

        static PyObject* python(
            PyObject* capsule,
            PyObject* const* args,
            Py_ssize_t nargs,
            PyObject* kwnames
        ) {
            try {
                return call_python(
                    std::make_index_sequence<sizeof...(Target)>{},
                    capsule,
                    args,
                    nargs,
                    kwnames
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static std::function<Return(Target...)> cpp(
            PyObject* func,
            const Defaults& defaults
        ) {
            return [func, &defaults](Target... args) {
                PyObject* result = PyObject_VectorCall(func, args...);
                if (result == nullptr) {
                    Exception::from_python();
                }
                return static_cast<Return>(reinterpret_steal<Object>(result));
            };
        }

    };

    CapsuleContents* contents;
    PyObject* m_ptr;
    bool owns_contents;

    struct from_python_t {};

    // TODO: inefficient -> creates defaults twice.  Perhaps I need to make a separate
    // constructor for this.  It's also probably too similar to the other constructor.
    // I should use from_python_t{}.

    // // TODO: private constructor needs to take a PyObject* function and then generate
    // // an equivalent C++ function from it.
    // template <typename... Values>
    // Function_(std::string name, PyObject* func, Values&&... defaults) :
    //     contents(new CapsuleContents {
    //         name,
    //         Def<call_policy>::cpp(func, ParseDefaults<Values...>::build(defaults...)),
    //         ParseDefaults<Values...>::build(defaults...)
    //     }),
    //     m_ptr(func),
    //     owns_contents(true)
    // {}

public:

    /* Construct a py::Function from a valid C++ function object with the templated
    signature.  Use CTAD to deduce the signature if not explicitly provided.  If the
    signature contains default value annotations, they must be specified here. */
    template <typename Func, typename... Values>
        requires (
            std::is_invocable_r_v<Return, Func, Target...> &&
            ParseDefaults<Values...>::enable
        )
    Function_(std::string name, Func&& func, Values&&... defaults) :
        contents(new CapsuleContents {
            name,
            std::forward<Func>(func),
            ParseDefaults<Values...>::build(std::forward<Values>(defaults)...)
        }),
        m_ptr(wrap_python(contents)),
        owns_contents(false)
    {}

    ~Function_() {
        Py_XDECREF(m_ptr);
        if (owns_contents) {
            delete contents;
        }
    }

    // TODO: internal constructor needs to take default values before the function
    // object itself, so that the function can be created using the correct defaults.

    // /* Borrow a reference to an existing Python object and set the default values
    // accordingly.  This effectively replaces `reinterpret_borrow` for function types. */
    // template <typename... Values> requires (ParseDefaults<Values...>::enable)
    // static Function_ borrow(Handle func, Values&&... defaults) {
    //     PyObject* name = PyObject_GetAttrString(func.ptr(), "__name__");
    //     if (name == nullptr) {
    //         Exception::from_python();
    //     }
    //     std::string name_str = reinterpret_steal<Str>(name);

    //     // TODO: this is awkward.  I probably have to implement a protected constructor
    //     // to do this properly.
    //     Defaults values = ParseDefaults<Values...>::build(defaults...);
    //     return Function_(
    //         name_str,
    //         Def<call_policy>::cpp(func.ptr(), values),
    //         defaults...
    //     );
    // };

    // /* Steal a reference to an existing Python object and set the default values
    // accordingly.  This effectively replaces `reinterpet_steal` for function types. */
    // template <typename... Values> requires (ParseDefaults<Values...>::enable)
    // static Function_ steal(Handle func, Values&&... defaults) {
    //     PyObject* name = PyObject_GetAttrString(func.ptr(), "__name__");
    //     if (name == nullptr) {
    //         Exception::from_python();
    //     }
    //     std::string name_str = reinterpret_steal<Str>(name);

    //     // TODO: use same internal constructor as ::borrow()
    // }

    PyObject* ptr() const {
        return m_ptr;
    }

    /* Call the C++ function with the given arguments. */
    template <typename... Source> requires (Arguments<Source...>::enable)
    Return operator()(Source&&... args) const {
        return call(
            std::make_index_sequence<sizeof...(Target)>{},
            std::forward<Source>(args)...
        );
    }

    // TODO: if only it were this simple.  Because I'm using a PyCapsule to ensure the
    // lifecycle of the function object, the only way to get everything to work
    // correctly is to write a wrapper class that forwards the `self` argument to a
    // nested py::Function as the first argument.  That class would also need to be
    // exposed to Python, and would have to work the same way.  It would probably
    // also need to be a descriptor, so that it can be correctly attached to the
    // class as an instance method.  This ends up looking a lot like my current
    // @attachable decorator, but lower level and with more boilerplate.  I would also
    // only be able to expose the (*args, **kwargs) version, since templates can't be
    // transmitted up to Python.

    enum class Descr {
        METHOD,
        CLASSMETHOD,
        STATICMETHOD,
        PROPERTY
    };

    // TODO: rather than using an enum, just make separate C++ types and template
    // attach accordingly.

    // function.attach<py::Method>(type);
    // function.attach<py::Method>(type, "foo");

    /* Attach a descriptor with the same name as this function to the type, which
    forwards to this function when accessed. */
    void attach(Type& type, Descr policy = Descr::METHOD) {
        PyObject* descriptor = PyDescr_NewMethod(type.ptr(), contents->method_def);
        if (descriptor == nullptr) {
            Exception::from_python();
        }
        int rc = PyObject_SetAttrString(type.ptr(), contents->name.data(), descriptor);
        Py_DECREF(descriptor);
        if (rc) {
            Exception::from_python();
        }
    };

    /* Attach a descriptor with a custom name to the type, which forwards to this
    function when accessed. */
    void attach(Type& type, std::string name, Descr policy = Descr::METHOD) {
        // TODO: same as above.
    };

    // TODO: PyCFunctions do carry a __name__ attribute, so we can at least use that.

    // TODO: Perhaps `py::DynamicFunction` is a separate class after all, and only
    // represents a PyFunctionObject.

    // TODO: ::borrow/steal(name, handle, defaults...) replaces reinterpret_borrow/steal for
    // a function type, which is disabled due to special handling for default arguments.
    // This will borrow/steal the function object, and then consult Def to generate
    // a lambda that invokes it using an optimized calling convention.

    // TODO: what would be really nice is if I could infer the function name and
    // default values from the PyObject* itself, but that would preclude the ability to
    // wrap PyCFunction pointers, which don't have any facility to inspect default
    // values or the function name.  The only way to solve this would be to modify the
    // PyCFunction_Type itself or subclass it to add support for these features only
    // within the context of this class.  That would be a pretty big undertaking, but
    // it would make a decent PEP.

    // -> This also means I have to handle all of my optional argument logic just the
    // same as in C++ when I generate the 2-way Python bindings.  The PyCFunction
    // has to apply these defaults itself before invoking the C++ function.  Similarly,
    // the std::function has to insert them before invoking the Python function, in
    // case the defaults are different between the `py::Function` object and its
    // underlying Python function.

    // TODO: What if DynamicFunc was implemented such that adding or removing an
    // argument at the C++ level yielded a new `py::Function` object with the updated
    // signature?  This would enable me to enforce the argument validation at compile
    // time, and register new arguments with equivalent semantics.

    // TODO: syntax would be something along the lines of:

    // py::Function updated = func.arg(x = []{}, y = []{}, ...))

    /* Implicitly convert a py::Function to an instance of the std::function that it
    wraps.  Note that this disables Python calling conventions, including default
    values. */
    template <typename R, typename... Args>
        requires (
            std::convertible_to<Return, R> &&
            (std::convertible_to<Target, Args> && ...)
        )
    operator std::function<R(Args...)>() const {
        return contents->func;
    }

};


template <typename Return, typename... Target>
Function_<Return(Target...)>::CapsuleContents::CapsuleContents(
    std::string func_name,
    std::function<Return(Target...)> func,
    Defaults defaults
) : name(func_name),
    func(func),
    defaults(defaults),
    method_def(
        name.data(),
        (PyCFunction) &Def<call_policy>::python,
        Def<call_policy>::flags,
        nullptr
    )
{}


template <typename F, typename... D>
Function_(std::string, F, D...) -> Function_<
    typename impl::GetSignature<std::decay_t<F>>::type
>;


/* Compile-time factory for `UnboundArgument` tags. */
template <StaticStr name>
static constexpr impl::UnboundArg<name> arg_ {};


////////////////////////
////    FUNCTION    ////
////////////////////////


template <typename... Args>
struct __call__<Function, Args...>                              : Returns<Object> {};
template <std::derived_from<Function> T>
struct __getattr__<T, "__globals__">                            : Returns<Dict<Str, Object>> {};
template <std::derived_from<Function> T>
struct __getattr__<T, "__closure__">                            : Returns<Tuple<Object>> {};
template <std::derived_from<Function> T>
struct __getattr__<T, "__defaults__">                           : Returns<Tuple<Object>> {};
template <std::derived_from<Function> T>
struct __getattr__<T, "__code__">                               : Returns<Code> {};
template <std::derived_from<Function> T>
struct __getattr__<T, "__annotations__">                        : Returns<Dict<Str, Object>> {};
template <std::derived_from<Function> T>
struct __getattr__<T, "__kwdefaults__">                         : Returns<Dict<Str, Object>> {};
template <std::derived_from<Function> T>
struct __getattr__<T, "__func__">                               : Returns<Function> {};


/* Represents a statically-typed Python function in C++.  Note that this can either be
a direct Python function or a C++ function wrapped to look like a Python function to
calling code.  In the latter case, it will appear to Python as a built-in function, for
which no code object will be compiled. */
class Function : public Object {
    using Base = Object;

    inline PyObject* self() const {
        PyObject* result = this->ptr();
        if (PyMethod_Check(result)) {
            result = PyMethod_GET_FUNCTION(result);
        }
        return result;
    }

public:
    static const Type type;

    template <typename T>
    static consteval bool check() {
        return impl::is_callable_any<T>;
    }

    template <typename T>
    static constexpr bool check(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return check<T>();
        } else if constexpr (check<T>()) {
            return obj.ptr() != nullptr;
        } else if constexpr (impl::is_object_exact<T>) {
            return obj.ptr() != nullptr && (
                PyFunction_Check(obj.ptr()) ||
                PyCFunction_Check(obj.ptr()) ||
                PyMethod_Check(obj.ptr())
            );
        } else {
            return false;
        }
    }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    Function(Handle h, const borrowed_t& t) : Base(h, t) {}
    Function(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (check<T>())
    Function(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    Function(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Function>(accessor).release(), stolen_t{})
    {}

    /* Implicitly convert a C++ function or callable object into a py::Function. */
    template <impl::cpp_like T> requires (check<T>())
    Function(T&& func) :
        Base(pybind11::cpp_function(std::forward<T>(func)).release(), stolen_t{})
    {}

    /////////////////////////////
    ////    C++ INTERFACE    ////
    /////////////////////////////

    /* Get the module in which this function was defined. */
    inline Module module_() const {
        PyObject* result = PyFunction_GetModule(self());
        if (result == nullptr) {
            throw TypeError("function has no module");
        }
        return reinterpret_borrow<Module>(result);
    }

    /* Get the code object that is executed when this function is called. */
    inline Code code() const {
        if (PyCFunction_Check(this->ptr())) {
            throw RuntimeError("C++ functions do not have code objects");
        }
        PyObject* result = PyFunction_GetCode(self());
        if (result == nullptr) {
            throw RuntimeError("function does not have a code object");
        }
        return reinterpret_borrow<Code>(result);
    }

    /* Get the globals dictionary associated with the function object. */
    inline Dict<Str, Object> globals() const {
        PyObject* result = PyFunction_GetGlobals(self());
        if (result == nullptr) {
            Exception::from_python();
        }
        return reinterpret_borrow<Dict<Str, Object>>(result);
    }

    /* Get the name of the file from which the code was compiled. */
    inline std::string filename() const {
        return code().filename();
    }

    /* Get the first line number of the function. */
    inline size_t line_number() const {
        return code().line_number();
    }

    /* Get the function's base name. */
    inline std::string name() const {
        return code().name();
    }

    /* Get the function's qualified name. */
    inline std::string qualname() const {
        return code().qualname();
    }

    /* Get the total number of positional or keyword arguments for the function,
    including positional-only parameters and excluding keyword-only parameters. */
    inline size_t argcount() const {
        return code().argcount();
    }

    /* Get the number of positional-only arguments for the function.  Does not include
    variable positional or keyword arguments. */
    inline size_t posonlyargcount() const {
        return code().posonlyargcount();
    }

    /* Get the number of keyword-only arguments for the function.  Does not include
    positional-only or variable positional/keyword arguments. */
    inline size_t kwonlyargcount() const {
        return code().kwonlyargcount();
    }

    // /* Get a read-only dictionary mapping argument names to their default values. */
    // inline MappingProxy<Dict<Str, Object>> defaults() const {
    //     Code code = this->code();

    //     // check for positional defaults
    //     PyObject* pos_defaults = PyFunction_GetDefaults(self());
    //     if (pos_defaults == nullptr) {
    //         if (code.kwonlyargcount() > 0) {
    //             Object kwdefaults = attr<"__kwdefaults__">();
    //             if (kwdefaults.is(None)) {
    //                 return MappingProxy(Dict<Str, Object>{});
    //             } else {
    //                 return MappingProxy(reinterpret_steal<Dict<Str, Object>>(
    //                     kwdefaults.release())
    //                 );
    //             }
    //         } else {
    //             return MappingProxy(Dict<Str, Object>{});
    //         }
    //     }

    //     // extract positional defaults
    //     size_t argcount = code.argcount();
    //     Tuple<Object> defaults = reinterpret_borrow<Tuple<Object>>(pos_defaults);
    //     Tuple<Str> names = code.varnames()[{argcount - defaults.size(), argcount}];
    //     Dict result;
    //     for (size_t i = 0; i < defaults.size(); ++i) {
    //         result[names[i]] = defaults[i];
    //     }

    //     // merge keyword-only defaults
    //     if (code.kwonlyargcount() > 0) {
    //         Object kwdefaults = attr<"__kwdefaults__">();
    //         if (!kwdefaults.is(None)) {
    //             result.update(Dict(kwdefaults));
    //         }
    //     }
    //     return result;
    // }

    // /* Set the default value for one or more arguments.  If nullopt is provided,
    // then all defaults will be cleared. */
    // inline void defaults(Dict&& dict) {
    //     Code code = this->code();

    //     // TODO: clean this up.  The logic should go as follows:
    //     // 1. check for positional defaults.  If found, build a dictionary with the new
    //     // values and remove them from the input dict.
    //     // 2. check for keyword-only defaults.  If found, build a dictionary with the
    //     // new values and remove them from the input dict.
    //     // 3. if any keys are left over, raise an error and do not update the signature
    //     // 4. set defaults to Tuple(positional_defaults.values()) and update kwdefaults
    //     // in-place.

    //     // account for positional defaults
    //     PyObject* pos_defaults = PyFunction_GetDefaults(self());
    //     if (pos_defaults != nullptr) {
    //         size_t argcount = code.argcount();
    //         Tuple<Object> defaults = reinterpret_borrow<Tuple<Object>>(pos_defaults);
    //         Tuple<Str> names = code.varnames()[{argcount - defaults.size(), argcount}];
    //         Dict positional_defaults;
    //         for (size_t i = 0; i < defaults.size(); ++i) {
    //             positional_defaults[*names[i]] = *defaults[i];
    //         }

    //         // merge new defaults with old ones
    //         for (const Object& key : positional_defaults) {
    //             if (dict.contains(key)) {
    //                 positional_defaults[key] = dict.pop(key);
    //             }
    //         }
    //     }

    //     // check for keyword-only defaults
    //     if (code.kwonlyargcount() > 0) {
    //         Object kwdefaults = attr<"__kwdefaults__">();
    //         if (!kwdefaults.is(None)) {
    //             Dict temp = {};
    //             for (const Object& key : kwdefaults) {
    //                 if (dict.contains(key)) {
    //                     temp[key] = dict.pop(key);
    //                 }
    //             }
    //             if (dict) {
    //                 throw ValueError("no match for arguments " + Str(List(dict.keys())));
    //             }
    //             kwdefaults |= temp;
    //         } else if (dict) {
    //             throw ValueError("no match for arguments " + Str(List(dict.keys())));
    //         }
    //     } else if (dict) {
    //         throw ValueError("no match for arguments " + Str(List(dict.keys())));
    //     }

    //     // TODO: set defaults to Tuple(positional_defaults.values()) and update kwdefaults

    // }

    /* Get a read-only dictionary holding type annotations for the function. */
    inline MappingProxy<Dict<Str, Object>> annotations() const {
        PyObject* result = PyFunction_GetAnnotations(self());
        if (result == nullptr) {
            return MappingProxy(Dict<Str, Object>{});
        }
        return reinterpret_borrow<MappingProxy<Dict<Str, Object>>>(result);
    }

    // /* Set the type annotations for the function.  If nullopt is provided, then the
    // current annotations will be cleared.  Otherwise, the values in the dictionary will
    // be used to update the current values in-place. */
    // inline void annotations(std::optional<Dict> annotations) {
    //     if (!annotations.has_value()) {  // clear all annotations
    //         if (PyFunction_SetAnnotations(self(), Py_None)) {
    //             Exception::from_python();
    //         }

    //     } else if (!annotations.value()) {  // do nothing
    //         return;

    //     } else {  // update annotations in-place
    //         Code code = this->code();
    //         Tuple<Str> args = code.varnames()[{0, code.argcount() + code.kwonlyargcount()}];
    //         MappingProxy existing = this->annotations();

    //         // build new dict
    //         Dict result = {};
    //         for (const Object& arg : args) {
    //             if (annotations.value().contains(arg)) {
    //                 result[arg] = annotations.value().pop(arg);
    //             } else if (existing.contains(arg)) {
    //                 result[arg] = existing[arg];
    //             }
    //         }

    //         // account for return annotation
    //         static const Str s_return = "return";
    //         if (annotations.value().contains(s_return)) {
    //             result[s_return] = annotations.value().pop(s_return);
    //         } else if (existing.contains(s_return)) {
    //             result[s_return] = existing[s_return];
    //         }

    //         // check for unmatched keys
    //         if (annotations.value()) {
    //             throw ValueError(
    //                 "no match for arguments " +
    //                 Str(List(annotations.value().keys()))
    //             );
    //         }

    //         // push changes
    //         if (PyFunction_SetAnnotations(self(), result.ptr())) {
    //             Exception::from_python();
    //         }
    //     }
    // }

    /* Get the closure associated with the function.  This is a Tuple of cell objects
    containing data captured by the function. */
    inline Tuple<Object> closure() const {
        PyObject* result = PyFunction_GetClosure(self());
        if (result == nullptr) {
            return {};
        }
        return reinterpret_borrow<Tuple<Object>>(result);
    }

    /* Set the closure associated with the function.  If nullopt is given, then the
    closure will be deleted. */
    inline void closure(std::optional<Tuple<Object>> closure) {
        PyObject* item = closure ? closure.value().ptr() : Py_None;
        if (PyFunction_SetClosure(self(), item)) {
            Exception::from_python();
        }
    }

};


///////////////////////////
////    CLASSMETHOD    ////
///////////////////////////


template <std::derived_from<ClassMethod> T>
struct __getattr__<T, "__func__">                               : Returns<Function> {};
template <std::derived_from<ClassMethod> T>
struct __getattr__<T, "__wrapped__">                            : Returns<Function> {};


/* Represents a statically-typed Python `classmethod` object in C++.  Note that this
is a pure descriptor class, and is not callable by itself.  It behaves similarly to the
@classmethod decorator, and can be attached to py::Type objects through normal
attribute assignment. */
class ClassMethod : public Object {
    using Base = Object;

public:
    static const Type type;

    template <typename T>
    static consteval bool check() {
        return std::derived_from<T, ClassMethod>;
    }

    template <typename T>
    static constexpr bool check(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return check<T>();

        } else if constexpr (check<T>()) {
            return obj.ptr() != nullptr;

        } else if constexpr (impl::is_object_exact<T>) {
            if (obj.ptr() == nullptr) {
                return false;
            }
            int result = PyObject_IsInstance(
                obj.ptr(),
                (PyObject*) &PyClassMethodDescr_Type
            );
            if (result == -1) {
                Exception::from_python();
            }
            return result;

        } else {
            return false;
        }
    }

    ClassMethod(Handle h, const borrowed_t& t) : Base(h, t) {}
    ClassMethod(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (check<T>())
    ClassMethod(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    ClassMethod(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<ClassMethod>(accessor).release(), stolen_t{})
    {}

    /* Wrap an existing Python function as a classmethod descriptor. */
    ClassMethod(Function func) : Base(PyClassMethod_New(func.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Get the underlying function. */
    inline Function function() const {
        return reinterpret_steal<Function>(Object(attr<"__func__">()).release());
    }

};


////////////////////////////
////    STATICMETHOD    ////
////////////////////////////


template <std::derived_from<StaticMethod> T>
struct __getattr__<T, "__func__">                               : Returns<Function> {};
template <std::derived_from<StaticMethod> T>
struct __getattr__<T, "__wrapped__">                            : Returns<Function> {};


/* Represents a statically-typed Python `staticmethod` object in C++.  Note that this
is a pure descriptor class, and is not callable by itself.  It behaves similarly to the
@staticmethod decorator, and can be attached to py::Type objects through normal
attribute assignment. */
class StaticMethod : public Object {
    using Base = Object;

public:
    static const Type type;

    template <typename T>
    static consteval bool check() {
        return std::derived_from<T, StaticMethod>;
    }

    template <typename T>
    static constexpr bool check(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return check<T>();

        } else if constexpr (check<T>()) {
            return obj.ptr() != nullptr;

        } else if constexpr (impl::is_object_exact<T>) {
            if (obj.ptr() == nullptr) {
                return false;
            }
            int result = PyObject_IsInstance(
                obj.ptr(),
                (PyObject*) &PyStaticMethod_Type
            );
            if (result == -1) {
                Exception::from_python();
            }
            return result;

        } else {
            return false;
        }
    }

    StaticMethod(Handle h, const borrowed_t& t) : Base(h, t) {}
    StaticMethod(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (check<T>())
    StaticMethod(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    StaticMethod(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<StaticMethod>(accessor).release(), stolen_t{})
    {}

    /* Wrap an existing Python function as a staticmethod descriptor. */
    StaticMethod(Function func) : Base(PyStaticMethod_New(func.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            Exception::from_python();
        }
    }

    /* Get the underlying function. */
    inline Function function() const {
        return reinterpret_steal<Function>(Object(attr<"__func__">()).release());
    }

};


////////////////////////
////    PROPERTY    ////
////////////////////////


namespace impl {
    static const Type PyProperty = reinterpret_borrow<Type>(
        reinterpret_cast<PyObject*>(&PyProperty_Type)
    );
}


template <std::derived_from<Property> T>
struct __getattr__<T, "fget">                                   : Returns<Function> {};
template <std::derived_from<Property> T>
struct __getattr__<T, "fset">                                   : Returns<Function> {};
template <std::derived_from<Property> T>
struct __getattr__<T, "fdel">                                   : Returns<Function> {};
template <std::derived_from<Property> T>
struct __getattr__<T, "getter">                                 : Returns<Function> {};
template <std::derived_from<Property> T>
struct __getattr__<T, "setter">                                 : Returns<Function> {};
template <std::derived_from<Property> T>
struct __getattr__<T, "deleter">                                : Returns<Function> {};


/* Represents a statically-typed Python `property` object in C++.  Note that this is a
pure descriptor class, and is not callable by itself.  It behaves similarly to the
@property decorator, and can be attached to py::Type objects through normal attribute
assignment. */
class Property : public Object {
    using Base = Object;

public:
    static const Type type;

    template <typename T>
    static consteval bool check() {
        return std::derived_from<T, Property>;
    }

    template <typename T>
    static constexpr bool check(const T& obj) {
        if constexpr (impl::cpp_like<T>) {
            return check<T>();

        } else if constexpr (check<T>()) {
            return obj.ptr() != nullptr;

        } else if constexpr (impl::is_object_exact<T>) {
            if (obj.ptr() == nullptr) {
                return false;
            }
            int result = PyObject_IsInstance(
                obj.ptr(),
                impl::PyProperty.ptr());
            if (result == -1) {
                Exception::from_python();
            }
            return result;

        } else {
            return false;
        }
    }

    Property(Handle h, const borrowed_t& t) : Base(h, t) {}
    Property(Handle h, const stolen_t& t) : Base(h, t) {}

    template <impl::pybind11_like T> requires (check<T>())
    Property(T&& other) : Base(std::forward<T>(other)) {}

    template <typename Policy>
    Property(const pybind11::detail::accessor<Policy>& accessor) :
        Base(Base::from_pybind11_accessor<Property>(accessor).release(), stolen_t{})
    {}

    /* Wrap an existing Python function as a getter in a property descriptor. */
    Property(const Function& getter) :
        Base(impl::PyProperty(getter).release(), stolen_t{})
    {}

    /* Wrap existing Python functions as getter and setter in a property descriptor. */
    Property(const Function& getter, const Function& setter) :
        Base(impl::PyProperty(getter, setter).release(), stolen_t{})
    {}

    /* Wrap existing Python functions as getter, setter, and deleter in a property
    descriptor. */
    Property(const Function& getter, const Function& setter, const Function& deleter) :
        Base(impl::PyProperty(getter, setter, deleter).release(), stolen_t{})
    {}

    /* Get the function being used as a getter. */
    inline Function fget() const {
        return reinterpret_steal<Function>(Object(attr<"fget">()).release());
    }

    /* Get the function being used as a setter. */
    inline Function fset() const {
        return reinterpret_steal<Function>(Object(attr<"fset">()).release());
    }

    /* Get the function being used as a deleter. */
    inline Function fdel() const {
        return reinterpret_steal<Function>(Object(attr<"fdel">()).release());
    }

};


}  // namespace py
}  // namespace bertrand


namespace pybind11 {
namespace detail {

template <bertrand::py::impl::is_callable_any T>
struct type_caster<T> {
    PYBIND11_TYPE_CASTER(T, _("callable"));

    /* Convert a Python object into a C++ callable. */
    inline bool load(handle src, bool convert) {
        return false;
    }

    /* Convert a C++ callable into a Python object. */
    inline static handle cast(const T& src, return_value_policy policy, handle parent) {
        return bertrand::py::Function(src).release();
    }

};

}  // namespace detail
}  // namespace pybind11


#endif  // BERTRAND_PYTHON_FUNC_H
