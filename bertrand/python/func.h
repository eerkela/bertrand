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


#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#include <cstdlib>
#elif defined(_MSC_VER)
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
#endif


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


// TODO: descriptors might also allow for multiple dispatch if I manage it correctly.
// -> The descriptor would store an internal map of types to Python/C++ functions, and
// could be appended to from either side.  This would replace pybind11's overloading
// mechanism, and would store signatures in topographical order.  When the descriptor
// is called, it would test each signature in order, and call the first one that
// fully matches


namespace impl {
    struct ArgTag {};
}


/* A compile-time argument annotation that represents a bound positional or keyword
argument to a py::Function. */
template <StaticStr Name, typename T>
class Arg : public impl::ArgTag {

    template <bool positional, bool keyword>
    struct Optional : public impl::ArgTag {
        using type = T;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = positional;
        static constexpr bool is_keyword = keyword;
        static constexpr bool is_optional = true;
        static constexpr bool is_variadic = false;

        T value;

        template <std::convertible_to<T> V>
        Optional(V&& value) : value(std::forward<V>(value)) {}
        Optional(const Arg& other) : value(other.value) {}
        Optional(Arg&& other) : value(std::move(other.value)) {}

        operator std::remove_reference_t<T>&() & { return value; }
        operator std::remove_reference_t<T>&&() && { return std::move(value); }
        operator const std::remove_const_t<std::remove_reference_t<T>>&() const & {
            return value;
        }
    };

    template <bool optional>
    struct Positional : public impl::ArgTag {
        using type = T;
        using opt = Optional<true, false>;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = true;
        static constexpr bool is_keyword = false;
        static constexpr bool is_optional = optional;
        static constexpr bool is_variadic = false;

        T value;

        template <std::convertible_to<T> V>
        Positional(V&& value) : value(std::forward<V>(value)) {}
        Positional(const Arg& other) : value(other.m_value) {}
        Positional(Arg&& other) : value(std::move(other.m_value)) {}

        operator std::remove_reference_t<T>&() & { return value; }
        operator std::remove_reference_t<T>&&() && { return std::move(value); }
        operator const std::remove_const_t<std::remove_reference_t<T>>&() const & {
            return value;
        }
    };

    template <bool optional>
    struct Keyword : public impl::ArgTag {
        using type = T;
        using opt = Optional<false, true>;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = false;
        static constexpr bool is_keyword = true;
        static constexpr bool is_optional = optional;
        static constexpr bool is_variadic = false;

        T value;

        template <std::convertible_to<T> V>
        Keyword(V&& value) : value(std::forward<V>(value)) {}
        Keyword(const Arg& other) : value(other.m_value) {}
        Keyword(Arg&& other) : value(std::move(other.m_value)) {}

        operator std::remove_reference_t<T>&() & { return value; }
        operator std::remove_reference_t<T>&&() && { return std::move(value); }
        operator const std::remove_const_t<std::remove_reference_t<T>>&() const & {
            return value;
        }
    };

    struct Args : public impl::ArgTag {
        using type = T;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = true;
        static constexpr bool is_keyword = false;
        static constexpr bool is_optional = false;
        static constexpr bool is_variadic = true;

        std::vector<T> value;

        Args() = default;
        Args(const std::vector<T>& value) : value(value) {}
        Args(std::vector<T>&& value) : value(std::move(value)) {}
        template <std::convertible_to<T> V>
        Args(const std::vector<V>& value) {
            this->value.reserve(value.size());
            for (const auto& item : value) {
                this->value.push_back(item);
            }
        }
        Args(const Args& other) : value(other.value) {}
        Args(Args&& other) : value(std::move(other.value)) {}

        operator std::vector<T>&() & { return value; }
        operator std::vector<T>&&() && { return std::move(value); }
        operator const std::vector<T>&() const & { return value; }

        auto begin() const { return value.begin(); }
        auto cbegin() const { return value.cbegin(); }
        auto end() const { return value.end(); }
        auto cend() const { return value.cend(); }
        auto rbegin() const { return value.rbegin(); }
        auto crbegin() const { return value.crbegin(); }
        auto rend() const { return value.rend(); }
        auto crend() const { return value.crend(); }
        constexpr auto size() const { return value.size(); }
        constexpr auto empty() const { return value.empty(); }
        constexpr auto data() const { return value.data(); }
        constexpr decltype(auto) front() const { return value.front(); }
        constexpr decltype(auto) back() const { return value.back(); }
        constexpr decltype(auto) operator[](size_t index) const { return value.at(index); } 
    };

    struct Kwargs : public impl::ArgTag {
        using type = T;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = false;
        static constexpr bool is_keyword = true;
        static constexpr bool is_optional = false;
        static constexpr bool is_variadic = true;

        std::unordered_map<std::string, T> value;

        Kwargs();
        Kwargs(const std::unordered_map<std::string, T>& value) : value(value) {}
        Kwargs(std::unordered_map<std::string, T>&& value) : value(std::move(value)) {}
        template <std::convertible_to<T> V>
        Kwargs(const std::unordered_map<std::string, V>& value) {
            this->value.reserve(value.size());
            for (const auto& [k, v] : value) {
                this->value.emplace(k, v);
            }
        }
        Kwargs(const Kwargs& other) : value(other.value) {}
        Kwargs(Kwargs&& other) : value(std::move(other.value)) {}

        operator std::unordered_map<std::string, T>&() & { return value; }
        operator std::unordered_map<std::string, T>&&() && { return std::move(value); }
        operator const std::unordered_map<std::string, T>&() const & { return value; }

        auto begin() const { return value.begin(); }
        auto cbegin() const { return value.cbegin(); }
        auto end() const { return value.end(); }
        auto cend() const { return value.cend(); }
        constexpr auto size() const { return value.size(); }
        constexpr bool empty() const { return value.empty(); }
        constexpr bool contains(const std::string& key) const { return value.contains(key); }
        constexpr auto count(const std::string& key) const { return value.count(key); }
        decltype(auto) find(const std::string& key) const { return value.find(key); }
        decltype(auto) operator[](const std::string& key) const { return value.at(key); }
    };

public:
    static_assert(Name != "", "Argument name cannot be an empty string.");

    using type = T;
    using pos = Positional<false>;
    using kw = Keyword<false>;
    using opt = Optional<true, true>;
    using args = Args;
    using kwargs = Kwargs;
    static constexpr StaticStr name = Name;
    static constexpr bool is_positional = true;
    static constexpr bool is_keyword = true;
    static constexpr bool is_optional = false;
    static constexpr bool is_variadic = false;

    T value;

    template <std::convertible_to<T> V>
    Arg(V&& value) : value(std::forward<V>(value)) {}
    Arg(const Arg& other) : value(other.value) {}
    Arg(Arg&& other) : value(std::move(other.value)) {}

    operator std::remove_reference_t<T>&() & { return value; }
    operator std::remove_reference_t<T>&&() && { return std::move(value); }
    operator const std::remove_const_t<std::remove_reference_t<T>>&() const & {
        return value;
    }
};


namespace impl {

    /* A compile-time tag that allows for the familiar `py::arg<"name"> = value`
    syntax.  The `py::arg<"name">` part resolves to an instance of this class, and the
    argument becomes bound when the `=` operator is applied to it. */
    template <StaticStr name>
    struct UnboundArg {
        template <typename T>
        constexpr Arg<name, T> operator=(T&& value) const {
            return {std::forward<T>(value)};
        }
    };

    /* Introspect the proper signature for a py::Function instance from a generic
    function pointer, reference, or object, such as a lambda. */
    template <typename T>
    struct GetSignature;
    template <typename R, typename... A>
    struct GetSignature<R(*)(A...)> { using type = R(A...); };
    template <typename R, typename... A>
    struct GetSignature<R(*)(A...) noexcept> { using type = R(A...); };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...)> { using type = R(A...); };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) noexcept> { using type = R(A...); };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) const> { using type = R(A...); };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) const noexcept> { using type = R(A...); };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) volatile> { using type = R(A...); };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) volatile noexcept> { using type = R(A...); };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) const volatile> { using type = R(A...); };
    template <typename R, typename C, typename... A>
    struct GetSignature<R(C::*)(A...) const volatile noexcept> { using type = R(A...); };
    template <impl::has_call_operator T>
    struct GetSignature<T> { using type = GetSignature<decltype(&T::operator())>::type; };

    enum class CallPolicy {
        no_args,
        one_arg,
        positional,
        keyword
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
        function is called.  Since it is impossible to know the size of the container
        at compile time, this is the only way to enforce this constraint.
*/
template <typename F = Object(Arg<"args", Object>::args, Arg<"kwargs", Object>::kwargs)>  // TODO: const references?
class Function_ : public Function_<typename impl::GetSignature<F>::type> {};


template <typename Return, typename... Target>
class Function_<Return(Target...)> {
protected:

    /* Index into a parameter pack using template recursion. */
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

    template <typename T>
    class Inspect {
    public:
        using type                          = T;
        static constexpr StaticStr name     = "";
        static constexpr bool opt           = false;
        static constexpr bool pos           = true;
        static constexpr bool kw            = false;
        static constexpr bool kw_only       = false;
        static constexpr bool args          = false;
        static constexpr bool kwargs        = false;
    };

    template <typename T> requires (std::derived_from<std::decay_t<T>, impl::ArgTag>)
    class Inspect<T> {
        using U = std::decay_t<T>;
    public:
        using type                          = U::type;
        static constexpr StaticStr name     = U::name;
        static constexpr bool opt           = U::is_optional;
        static constexpr bool pos           = !U::is_variadic && !U::is_keyword;
        static constexpr bool kw            = !U::is_variadic && U::is_keyword;
        static constexpr bool kw_only       = !U::is_variadic && !U::is_positional;
        static constexpr bool args          = U::is_variadic && U::is_positional;
        static constexpr bool kwargs        = U::is_variadic && U::is_keyword;
    };

    template <typename... Args>
    class Signature {

        template <StaticStr name, typename... Ts>
        static constexpr size_t index_helper = 0;
        template <StaticStr name, typename T, typename... Ts>
        static constexpr size_t index_helper<name, T, Ts...> =
            (Inspect<T>::name == name) ? 0 : 1 + index_helper<name, Ts...>;

        template <typename... Ts>
        static constexpr size_t kw_index_helper = 0;
        template <typename T, typename... Ts>
        static constexpr size_t kw_index_helper<T, Ts...> =
            Inspect<T>::kw ? 0 : 1 + kw_index_helper<Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t kw_count_helper = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t kw_count_helper<I, T, Ts...> =
            kw_count_helper<I + Inspect<T>::kw, Ts...>;

        template <typename... Ts>
        static constexpr size_t kw_only_index_helper = 0;
        template <typename T, typename... Ts>
        static constexpr size_t kw_only_index_helper<T, Ts...> =
            Inspect<T>::kw_only ? 0 : 1 + kw_only_index_helper<Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t kw_only_count_helper = 0;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t kw_only_count_helper<I, T, Ts...> =
            kw_only_count_helper<I + Inspect<T>::kw_only, Ts...>;

        template <typename... Ts>
        static constexpr size_t opt_index_helper = 0;
        template <typename T, typename... Ts>
        static constexpr size_t opt_index_helper<T, Ts...> =
            Inspect<T>::opt ? 0 : 1 + opt_index_helper<Ts...>;

        template <size_t I, typename... Ts>
        static constexpr size_t opt_count_helper = I;
        template <size_t I, typename T, typename... Ts>
        static constexpr size_t opt_count_helper<I, T, Ts...> =
            opt_count_helper<I + Inspect<T>::opt, Ts...>;

        template <typename... Ts>
        static constexpr size_t args_index_helper = 0;
        template <typename T, typename... Ts>
        static constexpr size_t args_index_helper<T, Ts...> =
            Inspect<T>::args ? 0 : 1 + args_index_helper<Ts...>;

        template <typename... Ts>
        static constexpr size_t kwargs_index_helper = 0;
        template <typename T, typename... Ts>
        static constexpr size_t kwargs_index_helper<T, Ts...> =
            Inspect<T>::kwargs ? 0 : 1 + kwargs_index_helper<Ts...>;

    public:
        static constexpr size_t size = sizeof...(Args);

        /* Retrieve the (annotated) type at index I. */
        template <size_t I>
        using type = std::tuple_element<I, std::tuple<Args...>>::type;

        /* Find the index of the named argument, or `size` if the argument is not
        present. */
        template <StaticStr name>
        static constexpr size_t index = index_helper<name, Args...>;
        template <StaticStr name>
        static constexpr bool contains = index<name> != size;

        /* Get the index of the first keyword argument, or `size` if no keywords are
        present. */
        static constexpr size_t kw_index = kw_index_helper<Args...>;
        static constexpr size_t kw_count = kw_count_helper<0, Args...>;
        static constexpr bool has_kw = kw_index != size;

        /* Get the index of the first keyword-only argument, or `size` if no
        keyword-only arguments are present. */
        static constexpr size_t kw_only_index = kw_only_index_helper<Args...>;
        static constexpr size_t kw_only_count = kw_only_count_helper<0, Args...>;
        static constexpr bool has_kw_only = kw_only_index != size;

        /* Get the index of the first optional argument, or `size` if no optional
        arguments are present. */
        static constexpr size_t opt_index = opt_index_helper<Args...>;
        static constexpr size_t opt_count = opt_count_helper<0, Args...>;
        static constexpr bool has_opt = opt_index != size;

        /* Get the index of the first variadic positional argument, or `size` if
        variadic positional arguments are not allowed. */
        static constexpr size_t args_index = args_index_helper<Args...>;
        static constexpr bool has_args = args_index != size;

        /* Get the index of the first variadic keyword argument, or `size` if
        variadic keyword arguments are not allowed. */
        static constexpr size_t kwargs_index = kwargs_index_helper<Args...>;
        static constexpr bool has_kwargs = kwargs_index != size;

    private:

        template <size_t I, typename... Ts>
        static constexpr bool validate = true;

        template <size_t I, typename T, typename... Ts>
        static constexpr bool validate<I, T, Ts...> = [] {
            if constexpr (Inspect<T>::pos) {
                static_assert(
                    I == index<Inspect<T>::name> || Inspect<T>::name == "",
                    "signature must not contain multiple arguments with the same name"
                );
                static_assert(
                    I < kw_index,
                    "positional-only arguments must precede keywords"
                );
                static_assert(
                    I < args_index,
                    "positional-only arguments must precede variadic positional arguments"
                );
                static_assert(
                    I < kwargs_index,
                    "positional-only arguments must precede variadic keyword arguments"
                );
                static_assert(
                    I < opt_index || Inspect<T>::opt,
                    "all arguments after the first optional argument must also be optional"
                );

            } else if constexpr (Inspect<T>::kw) {
                static_assert(
                    I == index<Inspect<T>::name>,
                    "signature must not contain multiple arguments with the same name"
                );
                static_assert(
                    I < kw_only_index || Inspect<T>::kw_only,
                    "positional-or-keyword arguments must precede keyword-only arguments"
                );
                static_assert(
                    !(Inspect<T>::kw_only && has_args && I < args_index),
                    "keyword-only arguments must not precede variadic positional arguments"
                );
                static_assert(
                    I < kwargs_index,
                    "keyword arguments must precede variadic keyword arguments"
                );
                static_assert(
                    I < opt_index || Inspect<T>::opt || Inspect<T>::kw_only,
                    "all arguments after the first optional argument must also be optional"
                );

            } else if constexpr (Inspect<T>::args) {
                static_assert(
                    I < kwargs_index,
                    "variadic positional arguments must precede variadic keyword arguments"
                );
                static_assert(
                    I == args_index,
                    "signature must not contain multiple variadic positional arguments"
                );

            } else if constexpr (Inspect<T>::kwargs) {
                static_assert(
                    I == kwargs_index,
                    "signature must not contain multiple variadic keyword arguments"
                );
            }

            return validate<I + 1, Ts...>;
        }();

    public:

        /* If the target signature does not conform to Python calling conventions, throw
        an informative compile error. */
        static constexpr bool valid = validate<0, Args...>;

        /* Check whether the signature contains a keyword with the given name at
        runtime.  Use contains<"name"> to check at compile time instead. */
        static bool has_keyword(const char* name) {
            return ((std::strcmp(Inspect<Args>::name, name) == 0) || ...);
        }

    };

    using target = Signature<Target...>;
    static_assert(target::valid);

    class DefaultValues {

        template <size_t I>
        struct Value  {
            using annotation = target::template type<I>;
            using type = std::remove_cvref_t<typename Inspect<annotation>::type>;
            static constexpr StaticStr name = Inspect<annotation>::name;
            static constexpr size_t index = I;
            type value;
        };

        template <size_t I, typename Tuple, typename... Ts>
        struct CollectDefaults { using type = Tuple; };
        template <size_t I, typename... Defaults, typename T, typename... Ts>
        struct CollectDefaults<I, std::tuple<Defaults...>, T, Ts...> {
            template <typename U>
            struct Wrap {
                using type = CollectDefaults<
                    I + 1, std::tuple<Defaults...>, Ts...
                >::type;
            };
            template <typename U> requires (Inspect<U>::opt)
            struct Wrap<U> {
                using type = CollectDefaults<
                    I + 1, std::tuple<Defaults..., Value<I>>, Ts...
                >::type;
            };
            using type = Wrap<T>::type;
        };

        using tuple = CollectDefaults<0, std::tuple<>, Target...>::type;

        template <size_t I, typename... Ts>
        struct find { static constexpr size_t value = 0; };
        template <size_t I, typename T, typename... Ts>
        struct find<I, std::tuple<T, Ts...>> { static constexpr size_t value = 
            (I == T::index) ? 0 : 1 + find<I, std::tuple<Ts...>>::value;
        };

        /* Statically analyzes the arguments that are supplied to the function's
        constructor, so that they fully satisfy the default value annotations. */
        template <typename... Source>
        struct Parse {
            using source = Signature<Source...>;

            /* Recursively check whether the default values fully satisfy the target
            signature. */
            template <size_t I, size_t J>
            static constexpr bool enable_recursive = true;
            template <size_t I, size_t J> requires (I < target::opt_count && J < source::size)
            static constexpr bool enable_recursive<I, J> = [] {
                using D = std::tuple_element<I, tuple>::type;
                using V = source::template type<J>;
                if constexpr (Inspect<V>::pos) {
                    if constexpr (
                        !std::convertible_to<typename Inspect<V>::type, typename D::type>
                    ) {
                        return false;
                    }
                } else if constexpr (Inspect<V>::kw) {
                    if constexpr (
                        !target::template contains<Inspect<V>::name> ||
                        !source::template contains<D::name>
                    ) {
                        return false;
                    } else {
                        constexpr size_t idx = target::template index<Inspect<V>::name>;
                        using D2 = std::tuple_element<find<idx>::value, tuple>::type;
                        if constexpr (
                            !std::convertible_to<typename Inspect<V>::type, typename D2::type>
                        ) {
                            return false;
                        }
                    }
                } else {
                    return false;
                }
                return enable_recursive<I + 1, J + 1>;
            }();

            /* Constructor is only enabled if the default values are fully satisfied. */
            static constexpr bool enable =
                source::valid && target::opt_count == source::size && enable_recursive<0, 0>;

            template <size_t I>
            static constexpr decltype(auto) build_recursive(Source&&... values) {
                if constexpr (I < source::kw_index) {
                    return get_arg<I>(std::forward<Source>(values)...);
                } else {
                    using D = std::tuple_element<I, tuple>::type;
                    return get_arg<source::template index<D::name>>(
                        std::forward<Source>(values)...
                    ).value;
                }
            }

            /* Build the default values tuple from the provided arguments, reordering them
            as needed to account for keywords. */
            template <size_t... Is>
            static constexpr auto build(std::index_sequence<Is...>, Source&&... values) {
                return tuple(build_recursive<Is>(std::forward<Source>(values)...)...);
            }

        };

        tuple values;

    public:

        template <typename... Source> requires (Parse<Source...>::enable)
        DefaultValues(Source&&... source) : values(Parse<Source...>::build(
            std::make_index_sequence<sizeof...(Source)>{},
            std::forward<Source>(source)...
        )) {}

        DefaultValues(const DefaultValues& other) : values(other.values) {}
        DefaultValues(DefaultValues&& other) : values(std::move(other.values)) {}

        /* Constrain the function's constructor to enforce `::opt` annotations in
        the target signature. */
        template <typename... Source>
        static constexpr bool enable = Parse<Source...>::enable;

        /* Get the default value associated with the target argument at index I. */
        template <size_t I>
        const auto get() const {
            return std::get<find<I, tuple>::value>(values).value;
        };

    };

    template <typename... Source>
    class Arguments {
        using source = Signature<Source...>;

        template <size_t I>
        using T = target::template type<I>;
        template <size_t J>
        using S = source::template type<J>;

        template <size_t J, typename P>
        static constexpr bool check_target_args = true;
        template <size_t J, typename P> requires (J < source::kw_index)
        static constexpr bool check_target_args<J, P> = [] {
            if constexpr (Inspect<S<J>>::args) {
                if constexpr (!std::convertible_to<typename Inspect<S<J>>::type, P>) {
                    return false;
                }
            } else {
                if constexpr (!std::convertible_to<S<J>, P>) {
                    return false;
                }
            }
            return check_target_args<J + 1, P>;
        }();

        template <size_t J, typename KW>
        static constexpr bool check_target_kwargs = true;
        template <size_t J, typename KW> requires (J < source::size)
        static constexpr bool check_target_kwargs<J, KW> = [] {
            if constexpr (Inspect<S<J>>::kwargs) {
                if constexpr (!std::convertible_to<typename Inspect<S<J>>::type, KW>) {
                    return false;
                }
            } else {
                if constexpr (
                    (
                        target::has_args &&
                        target::template index<Inspect<S<J>>::name> < target::args_index
                    ) || (
                        !target::template contains<Inspect<S<J>>::name> &&
                        !std::convertible_to<typename Inspect<S<J>>::type, KW>
                    )
                ) {
                    return false;
                }
            }
            return check_target_kwargs<J + 1, KW>;
        }();

        template <size_t I, typename P>
        static constexpr bool check_source_args = true;
        template <size_t I, typename P> requires (I < target::size && I <= target::args_index)
        static constexpr bool check_source_args<I, P> = [] {
            if constexpr (Inspect<T<I>>::args) {
                if constexpr (!std::convertible_to<P, typename Inspect<T<I>>::type>) {
                    return false;
                }
            } else {
                if constexpr (
                    !std::convertible_to<P, typename Inspect<T<I>>::type> ||
                    source::template contains<Inspect<T<I>>::name>
                ) {
                    return false;
                }
            }
            return check_source_args<I + 1, P>;
        }();

        template <size_t I, typename KW>
        static constexpr bool check_source_kwargs = true;
        template <size_t I, typename KW> requires (I < target::size)
        static constexpr bool check_source_kwargs<I, KW> = [] {
            // TODO: does this work as expected?
            if constexpr (Inspect<T<I>>::kwargs) {
                if constexpr (!std::convertible_to<KW, typename Inspect<T<I>>::type>) {
                    return false;
                }
            } else {
                if constexpr (!std::convertible_to<KW, typename Inspect<T<I>>::type>) {
                    return false;
                }
            }
            return check_source_kwargs<I + 1, KW>;
        }();

        /* Recursively check whether the source arguments conform to Python calling
        conventions (i.e. no positional arguments after a keyword, no duplicate
        keywords, etc.), fully satisfy the target signature, and are convertible to the
        expected types, after accounting for parameter packs in both signatures. */
        template <size_t I, size_t J>
        static constexpr bool enable_recursive = true;
        template <size_t I, size_t J> requires (I < target::size && J >= source::size)
        static constexpr bool enable_recursive<I, J> = [] {
            if constexpr (Inspect<T<I>>::args || Inspect<T<I>>::opt) {
                return enable_recursive<I + 1, J>;
            } else if constexpr (Inspect<T<I>>::kwargs) {
                return check_target_kwargs<source::kw_index, typename Inspect<T<I>>::type>;
            }
            return false;
        }();
        template <size_t I, size_t J> requires (I >= target::size && J < source::size)
        static constexpr bool enable_recursive<I, J> = false;
        template <size_t I, size_t J> requires (I < target::size && J < source::size)
        static constexpr bool enable_recursive<I, J> = [] {
            // ensure target arguments are present & expand variadic parameter packs
            if constexpr (Inspect<T<I>>::pos) {
                if constexpr (
                    (J >= source::kw_index && !Inspect<T<I>>::opt) ||
                    (Inspect<T<I>>::name != "" && source::template contains<Inspect<T<I>>::name>)
                ) {
                    return false;
                }
            } else if constexpr (Inspect<T<I>>::kw_only) {
                if constexpr (
                    J < source::kw_index ||
                    (!source::template contains<Inspect<T<I>>::name> && !Inspect<T<I>>::opt)
                ) {
                    return false;
                }
            } else if constexpr (Inspect<T<I>>::kw) {
                if constexpr ((
                    J < source::kw_index &&
                    source::template contains<Inspect<T<I>>::name>
                ) || (
                    J >= source::kw_index &&
                    !source::template contains<Inspect<T<I>>::name> &&
                    !Inspect<T<I>>::opt
                )) {
                    return false;
                }
            } else if constexpr (Inspect<T<I>>::args) {
                if constexpr (!check_target_args<J, typename Inspect<T<I>>::type>) {
                    return false;
                }
                return enable_recursive<I + 1, source::kw_index>;
            } else if constexpr (Inspect<T<I>>::kwargs) {
                return check_target_kwargs<source::kw_index, typename Inspect<T<I>>::type>;
            } else {
                return false;
            }

            // validate source arguments & expand unpacking operators
            if constexpr (Inspect<S<J>>::pos) {
                if constexpr (!std::convertible_to<S<J>, T<I>>) {
                    return false;
                }
            } else if constexpr (Inspect<S<J>>::kw) {
                if constexpr (target::template contains<Inspect<S<J>>::name>) {
                    using type = T<target::template index<Inspect<S<J>>::name>>;
                    if constexpr (!std::convertible_to<S<J>, type>) {
                        return false;
                    }
                } else if constexpr (!target::has_kwargs) {
                    using type = Inspect<T<target::kwargs_idx>>::type;
                    if constexpr (!std::convertible_to<S<J>, type>) {
                        return false;
                    }
                }
            } else if constexpr (Inspect<S<J>>::args) {
                if constexpr (!check_source_args<I, typename Inspect<S<J>>::type>) {
                    return false;
                }
                return enable_recursive<target::args_index + 1, J + 1>;
            } else if constexpr (Inspect<S<J>>::kwargs) {
                return check_source_kwargs<I, typename Inspect<S<J>>::type>;
            } else {
                return false;
            }

            // advance to next argument pair
            return enable_recursive<I + 1, J + 1>;
        }();

        template <size_t I, typename T>
        static constexpr void build_kwargs(
            std::unordered_map<std::string, T>& map,
            Source&&... args
        ) {
            using Arg = S<source::kw_index + I>;
            if constexpr (!target::template contains<Inspect<Arg>::name>) {
                map.emplace(
                    Inspect<Arg>::name,
                    get_arg<source::kw_index + I>(std::forward<Source>(args)...)
                );
            }
        }

    public:

        /* Call operator is only enabled if source arguments are well-formed and match
        the target signature. */
        static constexpr bool enable = source::valid && enable_recursive<0, 0>;

        //////////////////////////
        ////    C++ -> C++    ////
        //////////////////////////

        /* The cpp() method is used to convert an index sequence over the target
         * signature into the corresponding values passed from the call site or drawn
         * from the function's defaults.  It is complicated by the presence of variadic
         * parameter packs in both the target signature and the call arguments, which
         * have to be handled as a cross product of possible combinations.  This yields
         * a total of 20 cases, which are represented as 5 separate specializations for
         * each of the possible target categories, as well as 4 different calling
         * conventions based on the presence of *args and/or **kwargs at the call site.
         *
         * Unless a variadic parameter pack is given at the call site, all of these
         * are resolved entirely at compile time by reordering the arguments using
         * template recursion.  However, because the size of a variadic parameter pack
         * cannot be determined at compile time, calls that use these will have to
         * extract values at runtime, and may therefore raise an error if a
         * corresponding value does not exist in the parameter pack, or if there are
         * extras that are not included in the target signature.
         */

        #define NO_UNPACK \
            template <size_t I>
        #define ARGS_UNPACK \
            template <size_t I, typename Iter, std::sentinel_for<Iter> End>
        #define KWARGS_UNPACK \
            template <size_t I, typename Mapping>
        #define ARGS_KWARGS_UNPACK \
            template <size_t I, typename Iter, std::sentinel_for<Iter> End, typename Mapping>

        NO_UNPACK
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            Source&&... args
        ) {
            if constexpr (I < source::kw_index) {
                return get_arg<I>(std::forward<Source>(args)...);
            } else {
                return defaults.template get<I>();
            }
        }

        NO_UNPACK requires (Inspect<T<I>>::kw && !Inspect<T<I>>::kw_only)
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            Source&&... args
        ) {
            if constexpr (I < source::kw_index) {
                return get_arg<I>(std::forward<Source>(args)...);
            } else if constexpr (source::template contains<Inspect<T<I>>::name>) {
                constexpr size_t idx = source::template index<Inspect<T<I>>::name>;
                return get_arg<idx>(std::forward<Source>(args)...);
            } else {
                return defaults.template get<I>();
            }
        }

        NO_UNPACK requires (Inspect<T<I>>::kw_only)
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            Source&&... args
        ) {
            if constexpr (source::template contains<Inspect<T<I>>::name>) {
                constexpr size_t idx = source::template index<Inspect<T<I>>::name>;
                return get_arg<idx>(std::forward<Source>(args)...);
            } else {
                return defaults.template get<I>();
            }
        }

        NO_UNPACK requires (Inspect<T<I>>::args)
        static constexpr auto cpp(
            const DefaultValues& defaults,
            Source&&... args
        ) {
            using Pack = std::vector<typename Inspect<T<I>>::type>;
            Pack vec;
            if constexpr (I < source::kw_index) {
                constexpr size_t diff = source::kw_index - I;
                vec.reserve(diff);
                []<size_t... Js>(
                    std::index_sequence<Js...>,
                    Pack& vec,
                    Source&&... args
                ) {
                    (vec.push_back(get_arg<I + Js>(std::forward<Source>(args)...)), ...);
                }(
                    std::make_index_sequence<diff>{},
                    vec,
                    std::forward<Source>(args)...
                );
            }
            return vec;
        }

        NO_UNPACK requires (Inspect<T<I>>::kwargs)
        static constexpr auto cpp(
            const DefaultValues& defaults,
            Source&&... args
        ) {
            using Pack = std::unordered_map<std::string, typename Inspect<T<I>>::type>;
            Pack pack;
            []<size_t... Js>(
                std::index_sequence<Js...>,
                Pack& pack,
                Source&&... args
            ) {
                (build_kwargs<Js>(pack, std::forward<Source>(args)...), ...);
            }(
                std::make_index_sequence<source::size - source::kw_index>{},
                pack,
                std::forward<Source>(args)...
            );
            return pack;
        }

        ARGS_UNPACK
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            if constexpr (I < source::kw_index) {
                return get_arg<I>(std::forward<Source>(args)...);
            } else {
                if (iter == end) {
                    if constexpr (Inspect<T<I>>::opt) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "could not unpack positional args - no match for "
                            "positional-only parameter at index " +
                            std::to_string(I)
                        );
                    }
                } else {
                    return *(iter++);
                }
            }
        }

        ARGS_UNPACK requires (Inspect<T<I>>::kw && !Inspect<T<I>>::kw_only)
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            if constexpr (I < source::kw_index) {
                return get_arg<I>(std::forward<Source>(args)...);
            } else if constexpr (source::template contains<Inspect<T<I>>::name>) {
                constexpr size_t idx = source::template index<Inspect<T<I>>::name>;
                return get_arg<idx>(std::forward<Source>(args)...);
            } else {
                if (iter == end) {
                    if constexpr (Inspect<T<I>>::opt) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "could not unpack positional args - no match for "
                            "parameter '" + std::string(T<I>::name) + "' at index " +
                            std::to_string(I)
                        );
                    }
                } else {
                    return *(iter++);
                }
            }
        }

        ARGS_UNPACK requires (Inspect<T<I>>::kw_only)
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            return cpp<I>(defaults, std::forward<Source>(args)...);  // no unpack
        }

        ARGS_UNPACK requires (Inspect<T<I>>::args)
        static constexpr auto cpp(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            using Pack = std::vector<typename Inspect<T<I>>::type>;
            Pack vec;
            if constexpr (I < source::args_index) {
                constexpr size_t diff = source::args_index - I;
                vec.reserve(diff + size);
                []<size_t... Js>(
                    std::index_sequence<Js...>,
                    Pack& vec,
                    Source&&... args
                ) {
                    (vec.push_back(get_arg<I + Js>(std::forward<Source>(args)...)), ...);
                }(
                    std::make_index_sequence<diff>{},
                    vec,
                    std::forward<Source>(args)...
                );
                vec.insert(vec.end(), iter, end);
            }
            return vec;
        }

        ARGS_UNPACK requires (Inspect<T<I>>::kwargs)
        static constexpr auto cpp(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            return cpp<I>(defaults, std::forward<Source>(args)...);  // no unpack
        }

        KWARGS_UNPACK
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            return cpp<I>(defaults, std::forward<Source>(args)...);  // no unpack
        }

        KWARGS_UNPACK requires (Inspect<T<I>>::kw && !Inspect<T<I>>::kw_only)
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            auto val = map.find(T<I>::name);
            if constexpr (I < source::kw_index) {
                if (val != map.end()) {
                    throw TypeError(
                        "duplicate value for parameter '" + std::string(T<I>::name) +
                        "' at index " + std::to_string(I)
                    );
                }
                return get_arg<I>(std::forward<Source>(args)...);
            } else if constexpr (source::template contains<Inspect<T<I>>::name>) {
                if (val != map.end()) {
                    throw TypeError(
                        "duplicate value for parameter '" + std::string(T<I>::name) +
                        "' at index " + std::to_string(I)
                    );
                }
                constexpr size_t idx = source::template index<Inspect<T<I>>::name>;
                return get_arg<idx>(std::forward<Source>(args)...);
            } else {
                if (val != map.end()) {
                    return *val;
                } else {
                    if constexpr (Inspect<T<I>>::opt) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "could not unpack keyword args - no match for "
                            "parameter '" + std::string(T<I>::name) + "' at index " +
                            std::to_string(I)
                        );
                    }
                }
            }
        }

        KWARGS_UNPACK requires (Inspect<T<I>>::kw_only)
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            auto val = map.find(T<I>::name);
            if constexpr (source::template contains<Inspect<T<I>>::name>) {
                if (val != map.end()) {
                    throw TypeError(
                        "duplicate value for parameter '" + std::string(T<I>::name) +
                        "' at index " + std::to_string(I)
                    );
                }
                constexpr size_t idx = source::template index<Inspect<T<I>>::name>;
                return get_arg<idx>(std::forward<Source>(args)...);
            } else {
                if (val != map.end()) {
                    return *val;
                } else {
                    if constexpr (Inspect<T<I>>::opt) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "could not unpack keyword args - no match for "
                            "parameter '" + std::string(T<I>::name) + "' at index " +
                            std::to_string(I)
                        );
                    }
                }
            }
        }

        KWARGS_UNPACK requires (Inspect<T<I>>::args)
        static constexpr auto cpp(
            const DefaultValues& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            return cpp<I>(defaults, std::forward<Source>(args)...);  // no unpack
        }

        KWARGS_UNPACK requires (Inspect<T<I>>::kwargs)
        static constexpr auto cpp(
            const DefaultValues& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            using Pack = std::unordered_map<std::string, typename Inspect<T<I>>::type>;
            Pack pack;
            []<size_t... Js>(
                std::index_sequence<Js...>,
                Pack& pack,
                Source&&... args
            ) {
                (build_kwargs<Js>(pack, std::forward<Source>(args)...), ...);
            }(
                std::make_index_sequence<source::size - source::kw_index>{},
                pack,
                std::forward<Source>(args)...
            );
            for (const auto& [key, value] : map) {
                if (pack.contains(key)) {
                    throw TypeError("duplicate value for parameter '" + key + "'");
                }
                pack[key] = value;
            }
            return pack;
        }

        ARGS_KWARGS_UNPACK
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            const Mapping& map,
            Source&&... args
        ) {
            return cpp<I>(
                defaults,
                size,
                iter,
                end,
                std::forward<Source>(args)...
            );  // positional unpack
        }

        ARGS_KWARGS_UNPACK requires (Inspect<T<I>>::kw && !Inspect<T<I>>::kw_only)
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            const Mapping& map,
            Source&&... args
        ) {
            auto val = map.find(T<I>::name);
            if constexpr (I < source::kw_index) {
                if (val != map.end()) {
                    throw TypeError(
                        "duplicate value for parameter '" + std::string(T<I>::name) +
                        "' at index " + std::to_string(I)
                    );
                }
                return get_arg<I>(std::forward<Source>(args)...);
            } else if constexpr (source::template contains<Inspect<T<I>>::name>) {
                if (val != map.end()) {
                    throw TypeError(
                        "duplicate value for parameter '" + std::string(T<I>::name) +
                        "' at index " + std::to_string(I)
                    );
                }
                constexpr size_t idx = source::template index<Inspect<T<I>>::name>;
                return get_arg<idx>(std::forward<Source>(args)...);
            } else {
                if (iter != end) {
                    return *(iter++);
                } else if (val != map.end()) {
                    return *val;
                } else {
                    if constexpr (Inspect<T<I>>::opt) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "could not unpack args - no match for parameter '" +
                            std::string(T<I>::name) + "' at index " +
                            std::to_string(I)
                        );
                    }
                }
            }
        }

        ARGS_KWARGS_UNPACK requires (Inspect<T<I>>::kw_only)
        static constexpr std::decay_t<typename Inspect<T<I>>::type> cpp(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            const Mapping& map,
            Source&&... args
        ) {
            return cpp<I>(
                defaults,
                map,
                std::forward<Source>(args)...
            );  // keyword unpack
        }

        ARGS_KWARGS_UNPACK requires (Inspect<T<I>>::args)
        static constexpr auto cpp(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            const Mapping& map,
            Source&&... args
        ) {
            return cpp<I>(
                defaults,
                size,
                iter,
                end,
                std::forward<Source>(args)...
            );  // positional unpack
        }

        ARGS_KWARGS_UNPACK requires (Inspect<T<I>>::kwargs)
        static constexpr auto cpp(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            const Mapping& map,
            Source&&... args
        ) {
            return cpp<I>(
                defaults,
                map,
                std::forward<Source>(args)...
            );  // keyword unpack
        }

        #undef NO_UNPACK
        #undef ARGS_UNPACK
        #undef KWARGS_UNPACK
        #undef ARGS_KWARGS_UNPACK

        /////////////////////////////
        ////    Python -> C++    ////
        /////////////////////////////

        /* NOTE: these wrappers will always use the vectorcall protocol where possible,
         * which is the fastest calling convention available in CPython.  This requires
         * us to parse or allocate an array of PyObject* pointers with a binary layout
         * that looks something like this:
         *
         *                          { kwnames tuple }
         *      -------------------------------------
         *      | x | p | p | p |...| k | k | k |...|
         *      -------------------------------------
         *            ^             ^
         *            |             nargs ends here
         *            *args starts here
         *
         * Where 'x' is an optional first element that can be temporarily written to
         * in order to efficiently forward the `self` argument for bound methods, etc.
         * The presence of this argument is determined by the
         * PY_VECTORCALL_ARGUMENTS_OFFSET flag, which is encoded in nargs.  You can
         * check for its presence by bitwise AND-ing against nargs, and the true
         * number of arguments must be extracted using `PyVectorcall_NARGS(nargs)`
         * to account for this.
         *
         * If PY_VECTORCALL_ARGUMENTS_OFFSET is set and 'x' is written to, then it must
         * always be reset to its original value before the function returns.  This
         * allows for nested forwarding/scoping using the same argument list, with no
         * extra allocations.
         */

        template <size_t I>
        static std::decay_t<typename Inspect<T<I>>::type> from_python(
            const DefaultValues& defaults,
            PyObject* const* array,
            size_t nargs,
            PyObject* kwnames,
            size_t kwcount
        ) {
            if (I < nargs) {
                return reinterpret_borrow<Object>(array[I]);
            }
            if constexpr (Inspect<T<I>>::opt) {
                return defaults.template get<I>();
            } else {
                throw TypeError(
                    "missing required positional-only argument at index " +
                    std::to_string(I)
                );
            }
        }

        template <size_t I> requires (Inspect<T<I>>::kw)
        static std::decay_t<typename Inspect<T<I>>::type> from_python(
            const DefaultValues& defaults,
            PyObject* const* array,
            size_t nargs,
            PyObject* kwnames,
            size_t kwcount
        ) {
            if (I < nargs) {
                return reinterpret_borrow<Object>(array[I]);
            } else if (kwnames != nullptr) {
                for (size_t i = 0; i < kwcount; ++i) {
                    const char* name = PyUnicode_AsUTF8(
                        PyTuple_GET_ITEM(kwnames, i)
                    );
                    if (name == nullptr) {
                        Exception::from_python();
                    } else if (std::strcmp(name, Inspect<T<I>>::name) == 0) {
                        return reinterpret_borrow<Object>(array[nargs + i]);
                    }
                }
            }
            if constexpr (Inspect<T<I>>::opt) {
                return defaults.template get<I>();
            } else {
                throw TypeError(
                    "missing required argument '" + std::string(T<I>::name) +
                    "' at index " + std::to_string(I)
                );
            }
        }

        template <size_t I> requires (Inspect<T<I>>::kw_only)
        static std::decay_t<typename Inspect<T<I>>::type> from_python(
            const DefaultValues& defaults,
            PyObject* const* array,
            size_t nargs,
            PyObject* kwnames,
            size_t kwcount
        ) {
            if (kwnames != nullptr) {
                for (size_t i = 0; i < kwcount; ++i) {
                    const char* name = PyUnicode_AsUTF8(
                        PyTuple_GET_ITEM(kwnames, i)
                    );
                    if (name == nullptr) {
                        Exception::from_python();
                    } else if (std::strcmp(name, Inspect<T<I>>::name) == 0) {
                        return reinterpret_borrow<Object>(array[i]);
                    }
                }
            }
            if constexpr (Inspect<T<I>>::opt) {
                return defaults.template get<I>();
            } else {
                throw TypeError(
                    "missing required keyword-only argument '" +
                    std::string(T<I>::name) + "'"
                );
            }
        }

        template <size_t I> requires (Inspect<T<I>>::args)
        static auto from_python(
            const DefaultValues& defaults,
            PyObject* const* array,
            size_t nargs,
            PyObject* kwnames,
            size_t kwcount
        ) {
            std::vector<typename Inspect<T<I>>::type> vec;
            for (size_t i = I; i < nargs; ++i) {
                vec.push_back(reinterpret_borrow<Object>(array[i]));
            }
            return vec;
        }

        template <size_t I> requires (Inspect<T<I>>::kwargs)
        static auto from_python(
            const DefaultValues& defaults,
            PyObject* const* array,
            size_t nargs,
            PyObject* kwnames,
            size_t kwcount
        ) {
            std::unordered_map<std::string, typename Inspect<T<I>>::type> map;
            if (kwnames != nullptr) {
                auto sequence = std::make_index_sequence<target::kw_count>{};
                for (size_t i = 0; i < kwcount; ++i) {
                    Py_ssize_t length;
                    const char* name = PyUnicode_AsUTF8AndSize(
                        PyTuple_GET_ITEM(kwnames, i),
                        &length
                    );
                    if (name == nullptr) {
                        Exception::from_python();
                    } else if (!target::has_keyword(sequence)) {
                        map.emplace(
                            std::string(name, length),
                            reinterpret_borrow<Object>(array[nargs + i])
                        );
                    }
                }
            }
            return map;
        }

        /////////////////////////////
        ////    C++ -> Python    ////
        /////////////////////////////

        template <size_t J>
        static void to_python(PyObject* kwnames, PyObject** array, Source&&... args) {
            try {
                array[J + 1] = as_object(
                    get_arg<J>(std::forward<Source>(args)...)
                ).release().ptr();
            } catch (...) {
                for (size_t i = 1; i <= J; ++i) {
                    Py_XDECREF(array[i]);
                }
            }
        }

        template <size_t J> requires (Inspect<S<J>>::kw)
        static void to_python(PyObject* kwnames, PyObject** array, Source&&... args) {
            try {
                PyObject* name = PyUnicode_FromStringAndSize(
                    Inspect<S<J>>::name,
                    Inspect<S<J>>::name.size()
                );
                if (name == nullptr) {
                    Exception::from_python();
                }
                PyTuple_SET_ITEM(kwnames, J - source::kw_index, name);
                array[J + 1] = as_object(
                    get_arg<J>(std::forward<Source>(args)...)
                ).release().ptr();
            } catch (...) {
                for (size_t i = 1; i <= J; ++i) {
                    Py_XDECREF(array[i]);
                }
            }
        }

        template <size_t J> requires (Inspect<S<J>>::args)
        static void to_python(PyObject* kwnames, PyObject** array, Source&&... args) {
            size_t curr = J + 1;
            try {
                const auto& var_args = get_arg<J>(std::forward<Source>(args)...);
                for (const auto& value : var_args) {
                    array[curr] = as_object(value).release().ptr();
                    ++curr;
                }
            } catch (...) {
                for (size_t i = 1; i < curr; ++i) {
                    Py_XDECREF(array[i]);
                }
            }
        }

        template <size_t J> requires (Inspect<S<J>>::kwargs)
        static void to_python(PyObject* kwnames, PyObject** array, Source&&... args) {
            size_t curr = J + 1;
            try {
                const auto& var_kwargs = get_arg<J>(std::forward<Source>(args)...);
                for (const auto& [key, value] : var_kwargs) {
                    PyObject* name = PyUnicode_FromStringAndSize(
                        key.data(),
                        key.size()
                    );
                    if (name == nullptr) {
                        Exception::from_python();
                    }
                    PyTuple_SET_ITEM(kwnames, curr - source::kw_index, name);
                    array[curr] = as_object(value).release().ptr();
                    ++curr;
                }
            } catch (...) {
                for (size_t i = 1; i < curr; ++i) {
                    Py_XDECREF(array[i]);
                }
            }
        }

    };

    template <typename Iter, std::sentinel_for<Iter> End>
    static void validate_args(Iter& iter, const End& end) {
        if (iter != end) {
            std::string message =
                "too many arguments in positional parameter pack: ['" + repr(*iter);
            while (++iter != end) {
                message += "', '";
                message += repr(*iter);
            }
            message += "']";
            throw TypeError(message);
        }
    }

    template <typename source, size_t... Is, typename Kwargs>
    static void validate_kwargs(std::index_sequence<Is...>, const Kwargs& kwargs) {
        std::vector<std::string> extra;
        for (const auto& [key, value] : kwargs) {
            bool is_empty = key == "";
            bool match = (
                (key == Inspect<typename target::template type<Is>>::name) || ...
            );
            if (is_empty || !match) {
                extra.push_back(key);
            }
        }
        if (!extra.empty()) {
            auto iter = extra.begin();
            auto end = extra.end();
            std::string message = "unexpected keyword arguments: ['" + repr(*iter);
            while (++iter != end) {
                message += "', '";
                message += repr(*iter);
            }
            message += "']";
            throw TypeError(message);
        }
    }

    /* A heap-allocated data structure that allows a C++ function to be efficiently
    passed between Python and C++ without losing any of its original properties.  A
    shared reference to this object is stored in both the `py::Function` instance and a
    special `PyCapsule` that is passed up to the PyCFunction wrapper.  It will be kept
    alive as long as either of these references are in scope, and allows additional
    references to be passed along whenever a `py::Function` is copied or moved,
    mirroring the reference count of the underlying PyObject*.

    The PyCapsule is annotated with the mangled function type, which includes the
    signature of the underlying C++ function.  By matching against this identifier,
    Bertrand can determine whether an arbitrary Python function is:
        1.  Backed by a py::Function object, in which case it will extract the
            underlying PyCapsule, and
        2.  Whether the receiving function exactly matches the signature of the
            original py::Function.

    If both of these conditions are met, Bertrand will unpack the C++ Capsule and take
    a new reference to it, extending its lifetime.  This avoids creating an additional
    wrapper around the Python function, and allows the function to be passed
    arbitrarily across the language boundary without introducing any overhead.

    If condition (1) is met but (2) is not, then a TypeError is raised that contains
    the mismatched signatures, which are demangled for clarity. */
    class Capsule {

        static std::string demangle(const char* name) {
            #if defined(__GNUC__) || defined(__clang__)
                int status = 0;
                std::unique_ptr<char, void(*)(void*)> res {
                    abi::__cxa_demangle(
                        name,
                        nullptr,
                        nullptr,
                        &status
                    ),
                    std::free
                };
                return (status == 0) ? res.get() : name;
            #elif defined(_MSC_VER)
                char undecorated_name[1024];
                if (UnDecorateSymbolName(
                    name,
                    undecorated_name,
                    sizeof(undecorated_name),
                    UNDNAME_COMPLETE
                )) {
                    return std::string(undecorated_name);
                } else {
                    return name;
                }
            #else
                return name; // fallback: no demangling
            #endif
        }

    public:
        static constexpr StaticStr capsule_name = "bertrand";
        static const char* capsule_id;
        std::string name;
        std::function<Return(Target...)> func;
        DefaultValues defaults;
        PyMethodDef method_def;

        /* Construct a Capsule around a C++ function with the given name and default
        values. */
        Capsule(
            std::string&& func_name,
            std::function<Return(Target...)>&& func,
            DefaultValues&& defaults
        ) : name(std::move(func_name)),
            func(std::move(func)),
            defaults(std::move(defaults)),
            method_def(
                name.c_str(),
                (PyCFunction) &Wrap<call_policy>::python,
                Wrap<call_policy>::flags,
                nullptr
            )
        {}

        /* This proxy is what's actually stored in the PyCapsule, so that it uses the
        same shared_ptr to the C++ Capsule at the Python level. */
        struct Reference {
            std::shared_ptr<Capsule> ptr;
        };

        /* Extract the Capsule from a Bertrand-enabled Python function or create a new
        one to represent a Python function at the C++ level. */
        static std::shared_ptr<Capsule> from_python(PyObject* func) {
            if (PyCFunction_Check(func)) {
                PyObject* self = PyCFunction_GET_SELF(func);
                if (PyCapsule_IsValid(self, capsule_name)) {
                    const char* id = (const char*)PyCapsule_GetContext(self);
                    if (id == nullptr) {
                        Exception::from_python();
                    } else if (std::strcmp(id, capsule_id) == 0) {
                        auto result = reinterpret_cast<Reference*>(
                            PyCapsule_GetPointer(self, capsule_name)
                        );
                        return result->ptr;  // shared_ptr copy

                    } else {
                        std::string message = "Incompatible function signatures:";
                        message += "\n    Expected: " + demangle(capsule_id);
                        message += "\n    Received: " + demangle(id);
                        throw TypeError(message);
                    }
                }
            }

            return nullptr;
        }

        /* PyCapsule deleter that releases the shared_ptr reference held by the Python
        function when it is garbage collected. */
        static void deleter(PyObject* capsule) {
            auto contents = reinterpret_cast<Reference*>(
                PyCapsule_GetPointer(capsule, capsule_name)
            );
            delete contents;
        }

        /* Get the C++ Capsule from the PyCapsule object that's passed as the `self`
        argument to the PyCFunction wrapper. */
        static Capsule* get(PyObject* capsule) {
            auto result = reinterpret_cast<Reference*>(
                PyCapsule_GetPointer(capsule, capsule_name)
            );
            if (result == nullptr) {
                Exception::from_python();
            }
            return result->ptr.get();
        }

        /* Build a PyCFunction wrapper around the C++ function object.  Uses a
        PyCapsule to properly manage memory and ferry the C++ function into Python. */
        static PyObject* to_python(std::shared_ptr<Capsule> contents) {
            Reference* ref = new Reference{contents};
            PyObject* py_capsule = PyCapsule_New(
                ref,
                capsule_name,
                &deleter
            );
            if (py_capsule == nullptr) {
                delete ref;
                Exception::from_python();
            }

            if (PyCapsule_SetContext(py_capsule, (void*)capsule_id)) {
                Py_DECREF(py_capsule);
                Exception::from_python();
            }

            PyObject* result = PyCFunction_New(&contents->method_def, py_capsule);
            Py_DECREF(py_capsule);  // PyCFunction now owns the only reference
            if (result == nullptr) {
                Exception::from_python();
            }
            return result;
        }

        // TODO: try to lift a bunch of behavior out of this class to shorten error
        // messages.

        /* Choose an optimized Python call protocol based on the target signature. */
        static constexpr impl::CallPolicy call_policy = [] {
            if constexpr (target::size == 0) {
                return impl::CallPolicy::no_args;
            } else if constexpr (
                target::size == 1 && Inspect<typename target::template type<0>>::pos
            ) {
                return impl::CallPolicy::one_arg;
            } else if constexpr (!target::has_kw && !target::has_kwargs) {
                return impl::CallPolicy::positional;
            } else {
                return impl::CallPolicy::keyword;
            }
        }();

        template <impl::CallPolicy policy, typename Dummy = void>
        struct Wrap;

        template <typename Dummy>
        struct Wrap<impl::CallPolicy::no_args, Dummy> {
            static constexpr int flags = METH_NOARGS;

            static PyObject* python(PyObject* capsule, PyObject* /* unused */) {
                try {
                    return as_object(get(capsule)->func()).release().ptr();
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            }

        };

        template <typename Dummy>
        struct Wrap<impl::CallPolicy::one_arg, Dummy> {
            static constexpr int flags = METH_O;

            static PyObject* python(PyObject* capsule, PyObject* obj) {
                try {
                    Capsule* contents = get(capsule);
                    if (obj == nullptr) {
                        if constexpr (target::has_opt) {
                            return as_object(contents->func(
                                contents->defaults.template get<0>()
                            )).release().ptr();
                        } else {
                            throw TypeError("missing required argument");
                        }
                    }
                    return as_object(contents->func(
                        reinterpret_borrow<Object>(obj)
                    )).release().ptr();
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            }

        };

        template <typename Dummy>
        struct Wrap<impl::CallPolicy::positional, Dummy> {
            static constexpr int flags = METH_FASTCALL;

            static PyObject* python(
                PyObject* capsule,
                PyObject* const* args,
                Py_ssize_t nargs
            ) {
                try {
                    return []<size_t... Is>(
                        std::index_sequence<Is...>,
                        PyObject* capsule,
                        PyObject* const* args,
                        Py_ssize_t nargs
                    ) {
                        Py_ssize_t true_nargs = PyVectorcall_NARGS(nargs);
                        if constexpr (!target::has_args) {
                            if (true_nargs > static_cast<Py_ssize_t>(target::size)) {
                                throw TypeError(
                                    "expected at most " + std::to_string(target::size) +
                                    " positional arguments, but received " +
                                    std::to_string(true_nargs)
                                );    
                            }
                        }
                        Capsule* contents = get(capsule);
                        return as_object(
                            contents->func(
                                Arguments<Target...>::template from_python<Is>(
                                    contents->defaults,
                                    args,
                                    true_nargs,
                                    nullptr,
                                    0
                                )...
                            )
                        ).release().ptr();
                    }(
                        std::make_index_sequence<target::size>{},
                        capsule,
                        args,
                        nargs
                    );
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            }

        };

        template <typename Dummy>
        struct Wrap<impl::CallPolicy::keyword, Dummy> {
            static constexpr int flags = METH_FASTCALL | METH_KEYWORDS;

            static PyObject* python(
                PyObject* capsule,
                PyObject* const* args,
                Py_ssize_t nargs,
                PyObject* kwnames
            ) {
                try {
                    return []<size_t... Is>(
                        std::index_sequence<Is...>,
                        PyObject* capsule,
                        PyObject* const* args,
                        Py_ssize_t nargs,
                        PyObject* kwnames
                    ) {
                        size_t true_nargs = PyVectorcall_NARGS(nargs);
                        if constexpr (!target::has_args) {
                            if (true_nargs > target::size) {
                                throw TypeError(
                                    "expected at most " + std::to_string(target::size) +
                                    " positional arguments, but received " +
                                    std::to_string(true_nargs)
                                );
                            }
                        }
                        size_t kwcount = 0;
                        if (kwnames != nullptr) {
                            kwcount = PyTuple_GET_SIZE(kwnames);
                            if constexpr (!target::has_kwargs) {
                                for (size_t i = 0; i < kwcount; ++i) {
                                    Py_ssize_t length;
                                    const char* name = PyUnicode_AsUTF8AndSize(
                                        PyTuple_GET_ITEM(kwnames, i),
                                        &length
                                    );
                                    if (name == nullptr) {
                                        Exception::from_python();
                                    } else if (!target::has_keyword(name)) {
                                        throw TypeError(
                                            "unexpected keyword argument '" +
                                            std::string(name, length) + "'"
                                        );
                                    }
                                }
                            }
                        }
                        Capsule* contents = get(capsule);
                        return as_object(
                            contents->func(
                                Arguments<Target...>::template from_python<Is>(
                                    contents->defaults,
                                    args,
                                    true_nargs,
                                    kwnames,
                                    kwcount
                                )...
                            )
                        ).release().ptr();
                    }(
                        std::make_index_sequence<target::size>{},
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

        };

    };

    std::shared_ptr<Capsule> contents;
    PyObject* m_ptr;

public:
    using Defaults = DefaultValues;

    /* Template constraint that evaluates to true if this function can be called with
    the templated argument types. */
    template <typename... Source>
    static constexpr bool invocable = Arguments<Source...>::enable;

    /* Construct a py::Function from a valid C++ function with the templated signature.
    Use CTAD to deduce the signature if not explicitly provided.  If the signature
    contains default value annotations, they must be specified here. */
    template <typename Func, typename... Values>
        requires (
            std::is_invocable_r_v<Return, Func, Target...> &&
            Defaults::template enable<Values...>
        )
    Function_(std::string name, Func&& func, Values&&... defaults) :
        contents(new Capsule{
            std::move(name),
            std::function(std::forward<Func>(func)),
            Defaults(std::forward<Values>(defaults)...)
        }),
        m_ptr(Capsule::to_python(contents))
    {}

    /* Copy constructor. */
    Function_(const Function_& other) : contents(other.contents), m_ptr(other.m_ptr) {
        Py_XINCREF(m_ptr);
    }

    /* Move constructor. */
    Function_(Function_&& other) : contents(std::move(other.contents)), m_ptr(other.m_ptr) {
        other.m_ptr = nullptr;
    }

    ~Function_() {
        Py_XDECREF(m_ptr);
    }

    /* Call an external Python function that matches the target signature using
    Python-style arguments. */
    template <typename... Source> requires (invocable<Source...>)
    static Return invoke_py(Handle func, Source&&... args) {
        return []<size_t... Is>(
            std::index_sequence<Is...>,
            const Handle& func,
            Source&&... args
        ) {
            using source = Signature<Source...>;
            PyObject* result;

            // if there are no arguments, we can use an optimized protocol
            if constexpr (source::size == 0) {
                result = PyObject_CallNoArgs(func.ptr());

            } else if constexpr (!source::has_args && !source::has_kwargs) {
                // if there is only one argument, we can use an optimized protocol
                if constexpr (source::size == 1) {
                    if constexpr (source::has_kw) {
                        result = PyObject_CallOneArg(
                            func.ptr(),
                            as_object(
                                get_arg<0>(std::forward<Source>(args)...).value
                            ).ptr()
                        );
                    } else {
                        result = PyObject_CallOneArg(
                            func.ptr(),
                            as_object(
                                get_arg<0>(std::forward<Source>(args)...)
                            ).ptr()
                        );
                    }

                // if there are no *args, **kwargs, we can stack allocate the argument
                // array with a fixed size
                } else {
                    PyObject* array[source::size + 1];
                    array[0] = nullptr;
                    PyObject* kwnames;
                    if constexpr (source::has_kw) {
                        kwnames = PyTuple_New(source::kw_count);
                    } else {
                        kwnames = nullptr;
                    }
                    (
                        Arguments<Source...>::template to_python<Is>(
                            kwnames,
                            array,  // TODO: 1-indexed
                            std::forward<Source>(args)...
                        ),
                        ...
                    );
                    Py_ssize_t npos = source::size - source::kw_count;
                    result = PyObject_Vectorcall(
                        func.ptr(),
                        array + 1,
                        npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        kwnames
                    );
                    for (size_t i = 1; i <= source::size; ++i) {
                        Py_XDECREF(array[i]);
                    }
                }

            // otherwise, we have to heap-allocate the array with a variable size
            } else if constexpr (source::has_args && !source::has_kwargs) {
                auto var_args = get_arg<source::args_index>(std::forward<Source>(args)...);
                size_t nargs = source::size - 1 + var_args.size();
                PyObject** array = new PyObject*[nargs + 1];
                array[0] = nullptr;
                PyObject* kwnames;
                if constexpr (source::has_kw) {
                    kwnames = PyTuple_New(source::kw_count);
                } else {
                    kwnames = nullptr;
                }
                (
                    Arguments<Source...>::template to_python<Is>(
                        kwnames,
                        array,
                        std::forward<Source>(args)...
                    ),
                    ...
                );
                Py_ssize_t npos = source::size - 1 - source::kw_count + var_args.size();
                result = PyObject_Vectorcall(
                    func.ptr(),
                    array + 1,
                    npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    nullptr
                );
                for (size_t i = 1; i <= source::size; ++i) {
                    Py_XDECREF(array[i]);
                }
                delete[] array;

            } else if constexpr (!source::has_args && source::has_kwargs) {
                auto var_kwargs = get_arg<source::kwargs_index>(std::forward<Source>(args)...);
                size_t nargs = source::size - 1 + var_kwargs.size();
                PyObject** array = new PyObject*[nargs + 1];
                array[0] = nullptr;
                PyObject* kwnames = PyTuple_New(source::kw_count + var_kwargs.size());
                (
                    Arguments<Source...>::template to_python<Is>(
                        kwnames,
                        array,
                        std::forward<Source>(args)...
                    ),
                    ...
                );
                Py_ssize_t npos = source::size - 1 - source::kw_count;
                result = PyObject_Vectorcall(
                    func.ptr(),
                    array + 1,
                    npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    kwnames
                );
                for (size_t i = 1; i <= source::size; ++i) {
                    Py_XDECREF(array[i]);
                }
                delete[] array;

            } else {
                auto var_args = get_arg<source::args_index>(std::forward<Source>(args)...);
                auto var_kwargs = get_arg<source::kwargs_index>(std::forward<Source>(args)...);
                size_t nargs = source::size - 2 + var_args.size() + var_kwargs.size();
                PyObject** array = new PyObject*[nargs + 1];
                array[0] = nullptr;
                PyObject* kwnames = PyTuple_New(source::kw_count + var_kwargs.size());
                (
                    Arguments<Source...>::template to_python<Is>(
                        kwnames,
                        array,
                        std::forward<Source>(args)...
                    ),
                    ...
                );
                size_t npos = source::size - 2 - source::kw_count + var_args.size();
                result = PyObject_Vectorcall(
                    func.ptr(),
                    array + 1,
                    npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    kwnames
                );
                for (size_t i = 1; i <= source::size; ++i) {
                    Py_XDECREF(array[i]);
                }
                delete[] array;
            }

            if (result == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(result);
        }(
            std::make_index_sequence<sizeof...(Source)>{},
            func,
            std::forward<Source>(args)...
        );
    }

    /* Call an external C++ function that matches the target signature using the
    given defaults and Python-style arguments. */
    template <typename Func, typename... Source>
        requires (std::is_invocable_r_v<Return, Func, Target...> && invocable<Source...>)
    static Return invoke_cpp(const Defaults& defaults, Func&& func, Source&&... args) {
        return []<size_t... Is>(
            std::index_sequence<Is...>,
            const Defaults& defaults,
            Func&& func,
            Source&&... args
        ) {
            using source = Signature<Source...>;
            if constexpr (!source::has_args && !source::has_kwargs) {
                return func(
                    Arguments<Source...>::template cpp<Is>(
                        defaults,
                        std::forward<Source>(args)...
                    )...
                );

            } else if constexpr (source::has_args && !source::has_kwargs) {
                auto var_args = get_arg<source::args_index>(std::forward<Source>(args)...);
                auto iter = var_args.begin();
                auto end = var_args.end();
                return func(
                    Arguments<Source...>::template cpp<Is>(
                        defaults,
                        var_args.size(),
                        iter,
                        end,
                        std::forward<Source>(args)...
                    )...
                );
                if constexpr (!target::has_args) {
                    validate_args(iter, end);
                }

            } else if constexpr (!source::has_args && source::has_kwargs) {
                auto var_kwargs = get_arg<source::kwargs_index>(std::forward<Source>(args)...);
                if constexpr (!target::has_kwargs) {
                    validate_kwargs(
                        std::make_index_sequence<target::size>{},
                        var_kwargs
                    );
                }
                return func(
                    Arguments<Source...>::template cpp<Is>(
                        defaults,
                        var_kwargs,
                        std::forward<Source>(args)...
                    )...
                );

            } else {
                auto var_kwargs = get_arg<source::kwargs_index>(std::forward<Source>(args)...);
                if constexpr (!target::has_kwargs) {
                    validate_kwargs(
                        std::make_index_sequence<target::size>{},
                        var_kwargs
                    );
                }
                auto var_args = get_arg<source::args_index>(std::forward<Source>(args)...);
                auto iter = var_args.begin();
                auto end = var_args.end();
                return func(
                    Arguments<Source...>::template cpp<Is>(
                        defaults,
                        var_args.size(),
                        iter,
                        end,
                        var_kwargs,
                        std::forward<Source>(args)...
                    )...
                );
                if constexpr (!target::has_args) {
                    validate_args(iter, end);
                }
            }
        }(
            std::make_index_sequence<target::size>{},
            defaults,
            std::forward<Func>(func),
            std::forward<Source>(args)...
        );
    }

    /* Call the function with the given arguments. */
    template <typename... Source> requires (invocable<Source...>)
    Return operator()(Source&&... args) const {
        if (contents == nullptr) {
            return invoke_py(m_ptr, std::forward<Source>(args)...);
        } else {
            return invoke_cpp(
                contents->defaults,
                contents->func,
                std::forward<Source>(args)...
            );
        }
    }

    PyObject* ptr() const {
        return m_ptr;
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

    // enum class Descr {
    //     METHOD,
    //     CLASSMETHOD,
    //     STATICMETHOD,
    //     PROPERTY
    // };

    // TODO: rather than using an enum, just make separate C++ types and template
    // attach accordingly.

    // function.attach<py::Method>(type);
    // function.attach<py::Method>(type, "foo");

    // /* Attach a descriptor with the same name as this function to the type, which
    // forwards to this function when accessed. */
    // void attach(Type& type, Descr policy = Descr::METHOD) {
    //     PyObject* descriptor = PyDescr_NewMethod(type.ptr(), contents->method_def);
    //     if (descriptor == nullptr) {
    //         Exception::from_python();
    //     }
    //     int rc = PyObject_SetAttrString(type.ptr(), contents->name.data(), descriptor);
    //     Py_DECREF(descriptor);
    //     if (rc) {
    //         Exception::from_python();
    //     }
    // };

    // /* Attach a descriptor with a custom name to the type, which forwards to this
    // function when accessed. */
    // void attach(Type& type, std::string name, Descr policy = Descr::METHOD) {
    //     // TODO: same as above.
    // };

    // TODO: PyCFunctions do carry a __name__ attribute, so we can at least use that.

    // TODO: Perhaps `py::DynamicFunction` is a separate class after all, and only
    // represents a PyFunctionObject.

    // TODO: What if DynamicFunc was implemented such that adding or removing an
    // argument at the C++ level yielded a new `py::Function` object with the updated
    // signature?  This would enable me to enforce the argument validation at compile
    // time, and register new arguments with equivalent semantics.

    // TODO: syntax would be something along the lines of:

    // py::Function updated = func.arg(x = []{}, y = []{}, ...))

    // -> I would probably need to allocate a new Capsule for each call, since the
    // underlying std::function<> would need to be updated to reflect the new signature.
    // Otherwise, the argument lists wouldn't match.  That might be able to just
    // delegate to the existing Capsule (which would necessitate another shared_ptr
    // reference), which would allow for kwargs-based forwarding, just like Python.

    // -> This would come with the extra overhead of creating a new Python function
    // every time a function's signature is changed, since it might reference the old
    // function when it is called from Python.

    // -> It's probably best to make this is a subclass that just stores a reference
    // to an existing Capsule* and then generates new wrappers that splice in values
    // dynamically from a map.  That way I don't alter the original function at all.
    // Then, these custom DynamicFuncs could be passed around just like other functions
    // and converting them to a py::Function will recover the original function.  That
    // also makes the implementation look closer to how it currently works on the
    // Python side.

};


template <typename F, typename... D>
Function_(std::string, F, D...) -> Function_<
    typename impl::GetSignature<std::decay_t<F>>::type
>;


template <typename R, typename... T>
inline const char* Function_<R(T...)>::Capsule::capsule_id = typeid(Function_).name();


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
