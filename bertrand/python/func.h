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

    template <typename>
    friend class Function_;

    template <bool positional, bool keyword>
    class Optional : public impl::ArgTag {

        /* Implementation detail:  Optional arguments need to be able to represent a
         * special, uninitialized state in order to be properly forwarded during Python
         * function calls.  Normally, the buffer will always be initialized, either
         * with an explicit value supplied at the call site or a default value provided
         * to the `py::Function` constructor.  However, if the py::Function is backed
         * by a Python function object, then no such defaults will be provided.  As
         * such, the buffer will be left uninitialized, which causes the argument to be
         * omitted from the final invocation.  This allows Python to supply its own
         * default values, and means we don't have to redefine them unnecessarily.
         *
         * Users should never need to consider this, as it is handled automatically by
         * the `py::Function` class.  The uninitialized state never leaks out to the
         * public interface, and is heavily constrained so we can safely ignore it in
         * practice.
         */

        template <typename V>
        struct storage {
            alignas(V) char buffer[sizeof(V)];
            bool initialized;

            void assume_initialized() const {
                #if defined(__clang__)
                    __builtin_assume(initialized);
                #elif defined(__GNUC__)
                    [[assume(initialized)]];
                #elif defined(_MSC_VER)
                    __assume(initialized);
                #endif
            }

            storage() : initialized(false) {}
            template <std::convertible_to<V> U>
            storage(U&& value) : initialized(true) {
                new (buffer) V(std::forward<U>(value));
            }
            storage(const storage& other) : initialized(other.initialized) {
                if (initialized) {
                    new (buffer) V(other.value());
                }
            }
            storage(storage&& other) : initialized(other.initialized) {
                if (initialized) {
                    new (buffer) V(std::move(other.value()));
                }
            }
            ~storage() noexcept {
                if (initialized) {
                    value().~V();
                }
            }
            bool has_value() const { return initialized; }
            V& value() & {
                assume_initialized();
                return reinterpret_cast<V&>(buffer);
            }
            V&& value() && {
                assume_initialized();
                return std::move(reinterpret_cast<V&>(buffer));
            }
            const V& value() const & {
                assume_initialized();
                return reinterpret_cast<const V&>(buffer);
            }
        };

        template <typename V> requires (std::is_reference_v<V>)
        struct storage<V> {
            std::remove_reference_t<V>* ptr;
            storage() : ptr(nullptr) {}
            storage(V&& value) : ptr(&value) {}
            storage(const storage& other) : ptr(other.ptr) {}
            storage(storage&& other) : ptr(other.ptr) { other.ptr = nullptr; }
            bool has_value() const { return ptr != nullptr; }
            V& value() & { return *ptr; }
            V&& value() && { return std::move(*ptr); }
            const V& value() const & { return *ptr; }
        };

        storage<T> m_value;

        template <typename>
        friend class Function_;

    public:
        using type = T;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = positional;
        static constexpr bool is_keyword = keyword;
        static constexpr bool is_optional = true;
        static constexpr bool is_variadic = false;

        Optional() : m_value() {}
        template <std::convertible_to<T> V>
        Optional(V&& value) : m_value(std::forward<V>(value)) {}
        Optional(const Arg& other) : m_value(other.m_value) {}
        Optional(Arg&& other) : m_value(std::move(other.m_value)) {}

        std::remove_reference_t<T>& value() & { return m_value.value(); }
        std::remove_reference_t<T>&& value() && { return std::move(m_value.value()); }
        const std::remove_const_t<std::remove_reference_t<T>>& value() const & { return m_value.value(); }
        operator T&() & { return value(); }
        operator T&&() && { return std::move(value()); }
        operator const std::remove_const_t<T>&() const & { return value(); }
        template <typename V> requires (std::convertible_to<T, V>)
        operator V() const { return value(); }
    };

    template <bool optional>
    class Positional : public impl::ArgTag {

        template <typename>
        friend class Function_;

        T m_value;

    public:
        using type = T;
        using opt = Optional<true, false>;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = true;
        static constexpr bool is_keyword = false;
        static constexpr bool is_optional = optional;
        static constexpr bool is_variadic = false;

        template <std::convertible_to<T> V>
        Positional(V&& value) : m_value(std::forward<V>(value)) {}
        Positional(const Arg& other) : m_value(other.m_value) {}
        Positional(Arg&& other) : m_value(std::move(other.m_value)) {}

        std::remove_reference_t<T>& value() & { return m_value; }
        std::remove_reference_t<T>&& value() && { return std::move(m_value); }
        const std::remove_const_t<std::remove_reference_t<T>>& value() const & { return m_value; }

        operator T&() & { return m_value; }
        operator T&&() && { return std::move(m_value); }
        operator const std::remove_const_t<T>&() const & { return m_value; }
        template <typename V> requires (std::convertible_to<T, V>)
        operator V() const { return m_value; }
    };

    template <bool optional>
    class Keyword : public impl::ArgTag {

        template <typename>
        friend class Function_;

        T m_value;

    public:
        using type = T;
        using opt = Optional<false, true>;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = false;
        static constexpr bool is_keyword = true;
        static constexpr bool is_optional = optional;
        static constexpr bool is_variadic = false;

        template <std::convertible_to<T> V>
        Keyword(V&& value) : m_value(std::forward<V>(value)) {}
        Keyword(const Arg& other) : m_value(other.m_value) {}
        Keyword(Arg&& other) : m_value(std::move(other.m_value)) {}

        std::remove_reference_t<T>& value() & { return m_value; }
        std::remove_reference_t<T>&& value() && { return std::move(m_value); }
        const std::remove_const_t<std::remove_reference_t<T>>& value() const & { return m_value; }

        operator T&() & { return m_value; }
        operator T&&() && { return std::move(m_value); }
        operator const std::remove_const_t<T>&() const & { return m_value; }
        template <typename V> requires (std::convertible_to<T, V>)
        operator V() const { return m_value; }
    };

    class Args : public impl::ArgTag {

        template <typename>
        friend class Function_;

        std::vector<T> m_value;

    public:
        using type = T;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = true;
        static constexpr bool is_keyword = false;
        static constexpr bool is_optional = false;
        static constexpr bool is_variadic = true;

        Args() = default;
        Args(const std::vector<T>& value) : m_value(value) {}
        Args(std::vector<T>&& value) : m_value(std::move(value)) {}
        template <std::convertible_to<T> V>
        Args(const std::vector<V>& value) {
            m_value.reserve(value.size());
            for (const auto& item : value) {
                m_value.push_back(item);
            }
        }
        Args(const Args& other) : m_value(other.m_value) {}
        Args(Args&& other) : m_value(std::move(other.m_value)) {}

        std::vector<T>& value() & { return m_value; }
        std::vector<T>&& value() && { return std::move(m_value); }
        const std::vector<T>& value() const & { return m_value; }

        operator std::vector<T>&() & { return m_value; }
        operator std::vector<T>&&() && { return std::move(m_value); }
        operator const std::vector<T>&() const & { return m_value; }
        template <typename V> requires (std::convertible_to<std::vector<T>, V>)
        operator V() const { return m_value; }

        auto begin() const { return m_value.begin(); }
        auto cbegin() const { return m_value.cbegin(); }
        auto end() const { return m_value.end(); }
        auto cend() const { return m_value.cend(); }
        auto rbegin() const { return m_value.rbegin(); }
        auto crbegin() const { return m_value.crbegin(); }
        auto rend() const { return m_value.rend(); }
        auto crend() const { return m_value.crend(); }
        constexpr auto size() const { return m_value.size(); }
        constexpr auto empty() const { return m_value.empty(); }
        constexpr auto data() const { return m_value.data(); }
        constexpr decltype(auto) front() const { return m_value.front(); }
        constexpr decltype(auto) back() const { return m_value.back(); }
        constexpr decltype(auto) operator[](size_t index) const { return m_value.at(index); } 
    };

    class Kwargs : public impl::ArgTag {

        template <typename>
        friend class Function_;

        std::unordered_map<std::string, T> m_value;

    public:
        using type = T;
        static constexpr StaticStr name = Name;
        static constexpr bool is_positional = false;
        static constexpr bool is_keyword = true;
        static constexpr bool is_optional = false;
        static constexpr bool is_variadic = true;

        Kwargs();
        Kwargs(const std::unordered_map<std::string, T>& value) : m_value(m_value) {}
        Kwargs(std::unordered_map<std::string, T>&& value) : m_value(std::move(m_value)) {}
        template <std::convertible_to<T> V>
        Kwargs(const std::unordered_map<std::string, V>& value) {
            m_value.reserve(value.size());
            for (const auto& [k, v] : value) {
                m_value.emplace(k, v);
            }
        }
        Kwargs(const Kwargs& other) : m_value(other.m_value) {}
        Kwargs(Kwargs&& other) : m_value(std::move(other.m_value)) {}

        std::unordered_map<std::string, T>& value() & { return m_value; }
        std::unordered_map<std::string, T>&& value() && { return std::move(m_value); }
        const std::unordered_map<std::string, T>& value() const & { return m_value; }

        operator std::unordered_map<std::string, T>&() & { return m_value; }
        operator std::unordered_map<std::string, T>&&() && { return std::move(m_value); }
        operator const std::unordered_map<std::string, T>&() const & { return m_value; }
        template <typename V> requires (std::convertible_to<std::unordered_map<std::string, T>, V>)
        operator V() const { return m_value; }

        auto begin() const { return m_value.begin(); }
        auto cbegin() const { return m_value.cbegin(); }
        auto end() const { return m_value.end(); }
        auto cend() const { return m_value.cend(); }
        constexpr auto size() const { return m_value.size(); }
        constexpr bool empty() const { return m_value.empty(); }
        constexpr bool contains(const std::string& key) const { return m_value.contains(key); }
        constexpr auto count(const std::string& key) const { return m_value.count(key); }
        decltype(auto) find(const std::string& key) const { return m_value.find(key); }
        decltype(auto) operator[](const std::string& key) const { return m_value.at(key); }
    };

    T m_value;

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

    template <std::convertible_to<T> V>
    Arg(V&& value) : m_value(std::forward<V>(value)) {}
    Arg(const Arg& other) : m_value(other.m_value) {}
    Arg(Arg&& other) : m_value(std::move(other.m_value)) {}

    std::remove_reference_t<T>& value() & { return m_value; }
    std::remove_reference_t<T>&& value() && { return std::move(m_value); }
    const std::remove_const_t<std::remove_reference_t<T>>& value() const & { return m_value; }

    operator std::remove_reference_t<T>&() & { return m_value; }
    operator std::remove_reference_t<T>&&() && { return std::move(m_value); }
    operator const std::remove_const_t<std::remove_reference_t<T>>&() const & { return m_value; }
    template <typename V> requires (std::convertible_to<T, V>)
    operator V() const { return m_value; }
};


namespace impl {

    /* A compile-time tag that allows for the familiar `py::arg<"name"> = value`
    syntax.  The `py::arg<"name">` bit resolves to an instance of this class, and the
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
and `->` operators, or by accessing their `.value()` member directly, which comprises
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
template <typename F = Object(Arg<"args", Object>::args, Arg<"kwargs", Object>::kwargs)>
class Function_ : public Function_<typename impl::GetSignature<F>::type> {};


template <typename Return, typename... Target>
class Function_<Return(Target...)> {
protected:

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

        template <typename... Ts>
        static constexpr size_t kw_only_index_helper = 0;
        template <typename T, typename... Ts>
        static constexpr size_t kw_only_index_helper<T, Ts...> =
            Inspect<T>::kw_only ? 0 : 1 + kw_only_index_helper<Ts...>;

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

        /* Get the index of the first keyword argument, or `size` if no keywords are
        present. */
        static constexpr size_t kw_index = kw_index_helper<Args...>;

        /* Get the index of the first keyword-only argument, or `size` if no
        keyword-only arguments are present. */
        static constexpr size_t kw_only_index = kw_only_index_helper<Args...>;

        /* Get the index of the first optional argument, or `size` if no optional
        arguments are present. */
        static constexpr size_t opt_index = opt_index_helper<Args...>;

        /* Get the total number of optional arguments. */
        static constexpr size_t opt_count = opt_count_helper<0, Args...>;

        /* Get the index of the first variadic positional argument, or `size` if
        variadic positional arguments are not allowed. */
        static constexpr size_t args_index = args_index_helper<Args...>;

        /* Get the index of the first variadic keyword argument, or `size` if
        variadic keyword arguments are not allowed. */
        static constexpr size_t kwargs_index = kwargs_index_helper<Args...>;

        template <StaticStr name>
        static constexpr bool contains = index<name> != size;
        static constexpr bool has_kw = kw_index != size;
        static constexpr bool has_kw_only = kw_only_index != size;
        static constexpr bool has_opt = opt_index != size;
        static constexpr bool has_args = args_index != size;
        static constexpr bool has_kwargs = kwargs_index != size;

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

        /* If the target signature does not conform to Python calling conventions, throw
        an informative compile error. */
        static constexpr bool valid = validate<0, Args...>;

    };

    using target = Signature<Target...>;
    static_assert(target::valid);

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
                    if constexpr (!std::convertible_to<V, typename D::type>) {
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
                        if constexpr (!std::convertible_to<V, typename D2::type>) {
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
                    );
                }
            }

            /* Build the default values tuple from the provided arguments, reordering them
            as needed to account for keywords. */
            template <size_t... Is>
            static constexpr tuple build(std::index_sequence<Is...>, Source&&... values) {
                return {{build_recursive<Is>(std::forward<Source>(values)...)}...};
            }

        };

        tuple values;

    public:

        template <typename... Source> requires (Parse<Source...>::enable)
        DefaultValues(Source&&... source) : values(Parse<Source...>::build(
            std::make_index_sequence<sizeof...(Source)>{},
            std::forward<Source>(source)...
        )) {}

        /* Constrain the function's constructor to enforce `::opt` annotations in
        the target signature. */
        template <typename... Source>
        static constexpr bool enable = Parse<Source...>::enable;

        /* Get the default value associated with the target argument at index I. */
        template <size_t I>
        auto get() const {
            return std::get<find<I, tuple>::value>(values).value;  // TODO: change to .value() when DefaultValue supports optionals
        };

    };

    template <typename... Source>
    class Arguments {

        template <size_t I, typename T>
        static constexpr void build_kwargs(
            std::unordered_map<std::string, T>& map,
            Source&&... args
        ) {
            using Arg = source::template type<source::kw_index + I>;
            if constexpr (!target::template contains<Inspect<Arg>::name>) {
                map.emplace(
                    Inspect<Arg>::name,
                    get_arg<source::kw_index + I>(std::forward<Source>(args)...)
                );
            }
        }

    public:
        using source = Signature<Source...>;

        template <size_t J, typename P>
        static constexpr bool check_target_args = true;
        template <size_t J, typename P> requires (J < source::kw_index)
        static constexpr bool check_target_args<J, P> = [] {
            using S = source::template type<J>;
            if constexpr (Inspect<S>::args) {
                if constexpr (!std::convertible_to<typename Inspect<S>::type, P>) {
                    return false;
                }
            } else {
                if constexpr (!std::convertible_to<S, P>) {
                    return false;
                }
            }
            return check_target_args<J + 1, P>;
        }();

        template <size_t J, typename KW>
        static constexpr bool check_target_kwargs = true;
        template <size_t J, typename KW> requires (J < source::size)
        static constexpr bool check_target_kwargs<J, KW> = [] {
            using S = source::template type<J>;
            if constexpr (Inspect<S>::kwargs) {
                if constexpr (!std::convertible_to<typename Inspect<S>::type, KW>) {
                    return false;
                }
            } else {
                if constexpr (
                    (
                        target::has_args &&
                        target::template index<Inspect<S>::name> < target::args_index
                    ) || (
                        !target::template contains<Inspect<S>::name> &&
                        !std::convertible_to<typename Inspect<S>::type, KW>
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
            using T = target::template type<I>;
            if constexpr (Inspect<T>::args) {
                if constexpr (!std::convertible_to<P, typename Inspect<T>::type>) {
                    return false;
                }
            } else {
                if constexpr (
                    !std::convertible_to<P, typename Inspect<T>::type> ||
                    source::template contains<Inspect<T>::name>
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
            using T = target::template type<I>;
            // TODO: does this work as expected?
            if constexpr (Inspect<T>::kwargs) {
                if constexpr (!std::convertible_to<KW, typename Inspect<T>::type>) {
                    return false;
                }
            } else {
                if constexpr (!std::convertible_to<KW, typename Inspect<T>::type>) {
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
            using T = target::template type<I>;
            if constexpr (Inspect<T>::args || Inspect<T>::opt) {
                return enable_recursive<I + 1, J>;
            } else if constexpr (Inspect<T>::kwargs) {
                return check_target_kwargs<source::kw_index, typename Inspect<T>::type>;
            }
            return false;
        }();
        template <size_t I, size_t J> requires (I >= target::size && J < source::size)
        static constexpr bool enable_recursive<I, J> = false;
        template <size_t I, size_t J> requires (I < target::size && J < source::size)
        static constexpr bool enable_recursive<I, J> = [] {
            using T = target::template type<I>;
            using S = source::template type<J>;

            // ensure target arguments are present & expand variadic parameter packs
            if constexpr (Inspect<T>::pos) {
                if constexpr (
                    (J >= source::kw_index && !Inspect<T>::opt) ||
                    (Inspect<T>::name != "" && source::template contains<Inspect<T>::name>)
                ) {
                    return false;
                }
            } else if constexpr (Inspect<T>::kw_only) {
                if constexpr (
                    J < source::kw_index ||
                    (!source::template contains<Inspect<T>::name> && !Inspect<T>::opt)
                ) {
                    return false;
                }
            } else if constexpr (Inspect<T>::kw) {
                if constexpr ((
                    J < source::kw_index &&
                    source::template contains<Inspect<T>::name>
                ) || (
                    J >= source::kw_index &&
                    !source::template contains<Inspect<T>::name> &&
                    !Inspect<T>::opt
                )) {
                    return false;
                }
            } else if constexpr (Inspect<T>::args) {
                if constexpr (!check_target_args<J, typename Inspect<T>::type>) {
                    return false;
                }
                return enable_recursive<I + 1, source::kw_index>;
            } else if constexpr (Inspect<T>::kwargs) {
                return check_target_kwargs<source::kw_index, typename Inspect<T>::type>;
            } else {
                return false;
            }

            // validate source arguments & expand unpacking operators
            if constexpr (Inspect<S>::pos) {
                if constexpr (!std::convertible_to<S, T>) {
                    return false;
                }
            } else if constexpr (Inspect<S>::kw) {
                if constexpr (target::template contains<Inspect<S>::name>) {
                    using type = target::template type<
                        target::template index<Inspect<S>::name>
                    >;
                    if constexpr (!std::convertible_to<S, type>) {
                        return false;
                    }
                } else if constexpr (!target::has_kwargs) {
                    using type = Inspect<
                        typename target::template type<target::kwargs_idx>
                    >::type;
                    if constexpr (!std::convertible_to<S, type>) {
                        return false;
                    }
                }
            } else if constexpr (Inspect<S>::args) {
                if constexpr (!check_source_args<I, typename Inspect<S>::type>) {
                    return false;
                }
                return enable_recursive<target::args_index + 1, J + 1>;
            } else if constexpr (Inspect<S>::kwargs) {
                return check_source_kwargs<I, typename Inspect<S>::type>;
            } else {
                return false;
            }

            // advance to next argument pair
            return enable_recursive<I + 1, J + 1>;
        }();

        /* Call operator is only enabled if source arguments are well-formed and match
        the target signature. */
        static constexpr bool enable = source::valid && enable_recursive<0, 0>;

        /* The unpack() method is used to convert an index sequence over the target
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
         *
         * NOTE: this method should only be called if `enable` evalutes to true, which
         * means we never have to consider missing/invalid/duplicate inputs.  Thus, we
         * can safely assume that every target argument has exactly one matching value
         * in either the call signature or default values.
         */

        #define NO_UNPACK \
            template <size_t I, typename T>
        #define POS_UNPACK \
            template <size_t I, typename T, typename Iter, std::sentinel_for<Iter> End>
        #define KW_UNPACK \
            template <size_t I, typename T, typename Mapping>
        #define POS_KW_UNPACK \
            template < \
                size_t I, \
                typename T, \
                typename Iter, \
                std::sentinel_for<Iter> End, \
                typename Mapping \
            >

        NO_UNPACK
        static constexpr decltype(auto) unpack(
            const DefaultValues& defaults,
            Source&&... args
        ) {
            if constexpr (I < source::kw_index) {
                return get_arg<I>(std::forward<Source>(args)...);
            } else {
                return defaults.template get<I>();
            }
        }

        NO_UNPACK requires (Inspect<T>::kw && !Inspect<T>::kw_only)
        static constexpr decltype(auto) unpack(
            const DefaultValues& defaults,
            Source&&... args
        ) {
            if constexpr (I < source::kw_index) {
                return get_arg<I>(std::forward<Source>(args)...);
            } else if constexpr (source::template contains<Inspect<T>::name>) {
                constexpr size_t idx = source::template index<Inspect<T>::name>;
                return get_arg<idx>(std::forward<Source>(args)...);
            } else {
                return defaults.template get<I>();
            }
        }

        NO_UNPACK requires (Inspect<T>::kw_only)
        static constexpr decltype(auto) unpack(
            const DefaultValues& defaults,
            Source&&... args
        ) {
            if constexpr (source::template contains<Inspect<T>::name>) {
                constexpr size_t idx = source::template index<Inspect<T>::name>;
                return get_arg<idx>(std::forward<Source>(args)...);
            } else {
                return defaults.template get<I>();
            }
        }

        NO_UNPACK requires (Inspect<T>::args)
        static constexpr decltype(auto) unpack(
            const DefaultValues& defaults,
            Source&&... args
        ) {
            using Vec = std::vector<typename Inspect<T>::type>;
            Vec vec;
            if constexpr (I < source::kw_index) {
                constexpr size_t diff = source::kw_index - I;
                vec.reserve(diff);
                []<size_t... Js>(
                    std::index_sequence<Js...>,
                    Vec& vec,
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

        NO_UNPACK requires (Inspect<T>::kwargs)
        static constexpr decltype(auto) unpack(
            const DefaultValues& defaults,
            Source&&... args
        ) {
            using Pack = std::unordered_map<std::string, typename Inspect<T>::type>;
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

        POS_UNPACK
        static constexpr decltype(auto) unpack(
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
                    if constexpr (Inspect<T>::opt) {
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

        POS_UNPACK requires (Inspect<T>::kw && !Inspect<T>::kw_only)
        static constexpr decltype(auto) unpack(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            if constexpr (I < source::kw_index) {
                return get_arg<I>(std::forward<Source>(args)...);
            } else if constexpr (source::template contains<Inspect<T>::name>) {
                constexpr size_t idx = source::template index<Inspect<T>::name>;
                return get_arg<idx>(std::forward<Source>(args)...);
            } else {
                if (iter == end) {
                    if constexpr (Inspect<T>::opt) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "could not unpack positional args - no match for "
                            "parameter '" + T::name + "' at index " +
                            std::to_string(I)
                        );
                    }
                } else {
                    return *(iter++);
                }
            }
        }

        POS_UNPACK requires (Inspect<T>::kw_only)
        static constexpr decltype(auto) unpack(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            return unpack<I, T>(defaults, std::forward<Source>(args)...);  // no unpack
        }

        POS_UNPACK requires (Inspect<T>::args)
        static constexpr decltype(auto) unpack(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            using Vec = std::vector<typename Inspect<T>::type>;
            Vec vec;
            if constexpr (I < source::args_index) {
                constexpr size_t diff = source::args_index - I;
                vec.reserve(diff + size);
                []<size_t... Js>(
                    std::index_sequence<Js...>,
                    Vec& vec,
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

        POS_UNPACK requires (Inspect<T>::kwargs)
        static constexpr decltype(auto) unpack(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            Source&&... args
        ) {
            return unpack<I, T>(defaults, std::forward<Source>(args)...);  // no unpack
        }

        KW_UNPACK
        static constexpr decltype(auto) unpack(
            const DefaultValues& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            return unpack<I, T>(defaults, std::forward<Source>(args)...);  // no unpack
        }

        KW_UNPACK requires (Inspect<T>::kw && !Inspect<T>::kw_only)
        static constexpr decltype(auto) unpack(
            const DefaultValues& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            auto val = map.find(T::name);
            if constexpr (I < source::kw_index) {
                if (val != map.end()) {
                    throw TypeError(
                        "duplicate value for parameter '" + T::name +
                        "' at index " + std::to_string(I)
                    );
                }
                return get_arg<I>(std::forward<Source>(args)...);
            } else if constexpr (source::template contains<Inspect<T>::name>) {
                if (val != map.end()) {
                    throw TypeError(
                        "duplicate value for parameter '" + T::name +
                        "' at index " + std::to_string(I)
                    );
                }
                constexpr size_t idx = source::template index<Inspect<T>::name>;
                return get_arg<idx>(std::forward<Source>(args)...);
            } else {
                if (val != map.end()) {
                    return *val;
                } else {
                    if constexpr (Inspect<T>::opt) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "could not unpack keyword args - no match for "
                            "parameter '" + T::name + "' at index " +
                            std::to_string(I)
                        );
                    }
                }
            }
        }

        KW_UNPACK requires (Inspect<T>::kw_only)
        static constexpr decltype(auto) unpack(
            const DefaultValues& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            auto val = map.find(T::name);
            if constexpr (source::template contains<Inspect<T>::name>) {
                if (val != map.end()) {
                    throw TypeError(
                        "duplicate value for parameter '" + T::name +
                        "' at index " + std::to_string(I)
                    );
                }
                constexpr size_t idx = source::template index<Inspect<T>::name>;
                return get_arg<idx>(std::forward<Source>(args)...);
            } else {
                if (val != map.end()) {
                    return *val;
                } else {
                    if constexpr (Inspect<T>::opt) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "could not unpack keyword args - no match for "
                            "parameter '" + T::name + "' at index " +
                            std::to_string(I)
                        );
                    }
                }
            }
        }

        KW_UNPACK requires (Inspect<T>::args)
        static constexpr decltype(auto) unpack(
            const DefaultValues& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            return unpack<I, T>(defaults, std::forward<Source>(args)...);  // no unpack
        }

        KW_UNPACK requires (Inspect<T>::kwargs)
        static constexpr decltype(auto) unpack(
            const DefaultValues& defaults,
            const Mapping& map,
            Source&&... args
        ) {
            using Pack = std::unordered_map<std::string, typename Inspect<T>::type>;
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

        POS_KW_UNPACK
        static constexpr decltype(auto) unpack(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            const Mapping& map,
            Source&&... args
        ) {
            return unpack<I, T>(
                defaults,
                size,
                iter,
                end,
                std::forward<Source>(args)...
            );  // positional unpack
        }

        POS_KW_UNPACK requires (Inspect<T>::kw && !Inspect<T>::kw_only)
        static constexpr decltype(auto) unpack(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            const Mapping& map,
            Source&&... args
        ) {
            auto val = map.find(T::name);
            if constexpr (I < source::kw_index) {
                if (val != map.end()) {
                    throw TypeError(
                        "duplicate value for parameter '" + T::name +
                        "' at index " + std::to_string(I)
                    );
                }
                return get_arg<I>(std::forward<Source>(args)...);
            } else if constexpr (source::template contains<Inspect<T>::name>) {
                if (val != map.end()) {
                    throw TypeError(
                        "duplicate value for parameter '" + T::name +
                        "' at index " + std::to_string(I)
                    );
                }
                constexpr size_t idx = source::template index<Inspect<T>::name>;
                return get_arg<idx>(std::forward<Source>(args)...);
            } else {
                if (iter != end) {
                    return *(iter++);
                } else if (val != map.end()) {
                    return *val;
                } else {
                    if constexpr (Inspect<T>::opt) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "could not unpack args - no match for parameter '" +
                            T::name + "' at index " + std::to_string(I)
                        );
                    }
                }
            }
        }

        POS_KW_UNPACK requires (Inspect<T>::kw_only)
        static constexpr decltype(auto) unpack(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            const Mapping& map,
            Source&&... args
        ) {
            return unpack<I, T>(
                defaults,
                map,
                std::forward<Source>(args)...
            );  // keyword unpack
        }

        POS_KW_UNPACK requires (Inspect<T>::args)
        static constexpr decltype(auto) unpack(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            const Mapping& map,
            Source&&... args
        ) {
            return unpack<I, T>(
                defaults,
                size,
                iter,
                end,
                std::forward<Source>(args)...
            );  // positional unpack
        }

        POS_KW_UNPACK requires (Inspect<T>::kwargs)
        static constexpr decltype(auto) unpack(
            const DefaultValues& defaults,
            size_t size,
            Iter& iter,
            const End& end,
            const Mapping& map,
            Source&&... args
        ) {
            return unpack<I, T>(
                defaults,
                map,
                std::forward<Source>(args)...
            );  // keyword unpack
        }

        #undef NO_UNPACK
        #undef POS_UNPACK
        #undef KW_UNPACK
        #undef POS_KW_UNPACK
    };

    template <typename Iter, std::sentinel_for<Iter> End>
    static void validate_args(Iter& iter, const End& end) {
        if (iter != end) {
            std::string message =
                "too many arguments in positional parameter pack: ['" + *iter;
            while (iter != end) {
                message += "', '";
                message += repr(*(iter++));
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
            std::string message = "unexpected keyword arguments: ['" + *iter;
            while (++iter != end) {
                message += "', '";
                message += *iter;
            }
            message += "']";
            throw TypeError(message);
        }
    }

    // TODO: there might actually be a way to respect default values of a Python
    // function caught in C++, and that is to just not insert values for these when
    // calling it from C++.  That would allow me to implement reinterpret_borrow/steal
    // for this class, without the need to supply an extra set of default values
    // during construction.
    // -> Maybe defaults can be stored as optionals?  They're not going to hold
    // reference types anyways, so this shouldn't be a problem, right?  Then, when
    // I catch a Python function, I leave them all uninitialized, and don't include
    // them in the final argument array.
    // -> This totally works, and since PyCFunctions still maintain a __name__
    // attribute, I can fully reconstruct the function either way.  That means that
    // I can maintain my current control struct architecture and get static typing
    // for free.
    // -> This means that optional arguments have to also have some way of
    // representing a missing value, since the C++ function will always need to
    // accept a value for every argument in the target signature.  They also have to
    // represent reference types at the same time, so I'd need a structure like this:

    //  struct Optional {
    //      struct Value {
    //          T value;  // T can be a reference type
    //      }
    //      std::optional<Value> value;  // std::optional holds a reference by proxy
    //  }

    // TODO: then, if Optional is left uninitialized, it is automatically replaced
    // by the appropriate default when calling the function, either from the defaults
    // tuple or simply omitted during a Python call.

    // -> The DefaultValues stored within the tuple must also be optional for this to
    // work, since the C++ call will simply copy this value into the argument list.
    // Since it will be illegal to construct a py::Function from C++ without supplying
    // these values, the only way they'll be left uninitialized is if we're wrapping
    // a Python function using reinterpret_borrow/steal.

    /* A heap-allocated data structure that holds the core members of the function
    object, which are shared between Python and C++.  A reference to this object is
    stored in both the `py::Function` instance and a special `PyCapsule` that is
    passed up to the PyCFunction wrapper.  It will be kept alive as long as either of
    these references are in scope, and allows additional references to be passed along
    whenever a `py::Function` is copied or moved, mirroring the reference counts of the
    underlying PyObject*.

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
    arbitrarily across the language boundary without losing any of its original
    properties.

    If condition (1) is met but (2) is not, then a TypeError is raised that contains
    the mismatched signatures, which are demangled for clarity where possible. */
    class Capsule {

        struct from_python_t {};

        explicit Capsule(
            const from_python_t&,
            std::string func_name,
            std::function<Return(Target...)> func,
            DefaultValues defaults
        ) : name(func_name),
            func(func),
            defaults(defaults),
            method_def(
                name.c_str(),
                (PyCFunction) &Wrap<call_policy>::python,
                Wrap<call_policy>::flags,
                nullptr
            )
        {}

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
        static constexpr StaticStr capsule_name = "bt";
        static const char* capsule_id;
        std::string name;
        std::function<Return(Target...)> func;
        DefaultValues defaults;
        PyMethodDef method_def;

        /* Construct a Capsule around a C++ function with the given name and default
        values. */
        Capsule(
            std::string func_name,
            std::function<Return(Target...)> func,
            DefaultValues defaults
        ) : name(func_name),
            func(func),
            defaults(defaults),
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

        /* PyCapsule deleter that releases the shared_ptr reference held by the Python
        function when it is garbage collected. */
        static void deleter(PyObject* capsule) {
            auto contents = reinterpret_cast<Reference*>(
                PyCapsule_GetPointer(capsule, capsule_name)
            );
            delete contents;
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

            PyObject* name_obj = PyObject_GetAttrString(func, "__name__");
            if (name_obj == nullptr) {
                Exception::from_python();
            }

            Py_ssize_t name_len;
            const char* name_str = PyUnicode_AsUTF8AndSize(name_obj, &name_len);
            Py_DECREF(name_obj);
            if (name_str == nullptr) {
                Exception::from_python();
            }

            return new Capsule(
                from_python_t{},
                std::string(name_str, name_len),
                Wrap<call_policy>::cpp(),
                {}  // TODO: all defaults are optional, and are omitted from call
            );
        }

        // TODO: delete forward<>()

        /* C++ functions might define the target signature to take reference types,
        which can interfere with conversions when calling the function from Python.  In
        order to work around this, we store decayed arguments in a local tuple and then
        selectively move from them when calling std::apply.  This allows references to
        be respected at all times, without incurring any lifetime issues or compiler
        warnings about binding lvalues to converted temporaries.  */
        template <size_t I>
        static decltype(auto) forward(auto& val) {
            using T = target::template type<I>;
            if constexpr (std::is_lvalue_reference_v<typename Inspect<T>::type>) {
                return val;
            } else {
                return std::move(val);
            }
        }

        enum class CallPolicy {
            no_args,
            one_arg,
            positional,
            keyword
        };

        /* Choose an optimized Python call protocol based on the target signature. */
        static constexpr CallPolicy call_policy = [] {
            if constexpr (target::size == 0) {
                return CallPolicy::no_args;
            } else if constexpr (
                target::size == 1 && Inspect<typename target::template type<0>>::pos
            ) {
                return CallPolicy::one_arg;
            } else if constexpr (!target::has_kw && !target::has_kwargs) {
                return CallPolicy::positional;
            } else {
                return CallPolicy::keyword;
            }
        }();

        template <CallPolicy policy, typename Dummy = void>
        struct Wrap;

        // TODO: forwarding argument annotations to as_object forces a conversion to
        // Object rather than potentially reusing the current value if it is an object
        // subclass.  That adds extra overhead to the function call.

        template <typename Dummy>
        struct Wrap<CallPolicy::no_args, Dummy> {
            static constexpr int flags = METH_NOARGS;

            static PyObject* python(PyObject* capsule, PyObject* /* unused */) {
                try {
                    return as_object(
                        get(capsule)->func()
                    ).release().ptr();
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            }

            static std::function<Return(Target...)> cpp(
                PyObject* func,
                const DefaultValues& defaults
            ) {
                return [func, &defaults]() -> Return {
                    PyObject* result = PyObject_CallNoArgs(func);
                    if (result == nullptr) {
                        Exception::from_python();
                    }
                    return reinterpret_steal<Object>(result);
                };
            }

        };

        template <typename Dummy>
        struct Wrap<CallPolicy::one_arg, Dummy> {
            static constexpr int flags = METH_O;

            static PyObject* python(PyObject* capsule, PyObject* obj) {
                try {
                    return as_object(get(capsule)->func(
                        reinterpret_borrow<Object>(obj)
                    )).release().ptr();
                } catch (...) {
                    Exception::to_python();
                    return nullptr;
                }
            }

            static std::function<Return(Target...)> cpp(
                PyObject* func,
                const DefaultValues& defaults
            ) {
                return [func, &defaults](Target&&... args) -> Return {
                    PyObject* result = PyObject_CallOneArg(
                        func,
                        as_object(
                            get_arg<0>(std::forward<Target>(args)...).value()
                        ).ptr()
                    );
                    if (result == nullptr) {
                        Exception::from_python();
                    }
                    return reinterpret_steal<Object>(result);
                };
            }

        };

        /* NOTE: these wrappers will always use the vectorcall protocol if it is
         * enabled, which is the fastest calling convention available in CPython.  This
         * requires us to parse or allocate an array of PyObject* pointers with a
         * binary layout that looks something like this:
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

        template <typename Dummy>
        struct Wrap<CallPolicy::positional, Dummy> {
            static constexpr int flags = METH_FASTCALL;

            template <size_t I, typename T> requires (Inspect<T>::args)
            static auto python_arg(
                const DefaultValues& defaults,
                PyObject* const* args,
                Py_ssize_t nargs
            ) {
                using Vec = std::vector<std::decay_t<typename Inspect<T>::type>>;
                Vec var_args;
                for (Py_ssize_t i = I; i < nargs; ++i) {
                    var_args.push_back(reinterpret_borrow<Object>(args[i]));
                }
                return var_args;
            }

            template <size_t I, typename T> requires (!Inspect<T>::args)
            static std::decay_t<typename Inspect<T>::type> python_arg(
                const DefaultValues& defaults,
                PyObject* const* args,
                Py_ssize_t nargs
            ) {
                if (static_cast<Py_ssize_t>(I) < nargs) {
                    return reinterpret_borrow<Object>(args[I]);
                } else {
                    if constexpr (Inspect<T>::opt) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "missing required positional-only argument at "
                            "index " + std::to_string(I)
                        );
                    }
                }
            }

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
                        Capsule* contents = get(capsule);
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
                        return as_object(
                            contents->func(
                                python_arg<Is, typename target::template type<Is>>(
                                    contents->defaults,
                                    args,
                                    true_nargs
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

            // TODO: extract the guts of this into a separate invoke_py method that can
            // be called from the enclosing py::Function wrapper.
            // -> Also, handle the default value thing at the same time.

            template <size_t I, typename T>
            static void cpp_arg(PyObject** array, T&& arg) {
                // TODO: after doing default value refactor, this needs to check if the
                // argument is optional and not given, in which case it is not added to
                // the array.
                try {
                    // TODO: forwarding the argument annotation to as_object forces an
                    // extra conversion to Object, which is unnecessary if the value is
                    // already an Object subclass.
                    array[I + 1] = as_object(std::forward<T>(arg)).release().ptr();
                } catch (...) {
                    for (size_t i = 1; i <= I; ++i) {
                        Py_XDECREF(array[i]);
                    }
                    throw;
                }
            }

            // TODO: default value refactor makes this super complicated.  It means I
            // need to check the number of optional arguments that were given at the
            // call site and adjust the size of the array accordingly.

            // -> Maybe what I can do is hold a boolean that indicates whether the
            // underlying function is a C++ or Python function.  If It's a C++ function,
            // I do what I'm doing now and fully parse the signature.  If it's a
            // Python function, then while I'm parsing the arguments, I can check if
            // optional arguments are given at compile time.  If they aren't I omit
            // them from the argument array, which can be computed at compile time.

            // -> it might be possible to just track an offset instead.  That would
            // mean I still iterate over the optional arguments, but every time I find
            // an empty one, I add to an offset that is subtracted at every index.
            // That would overallocate the array, but it's almost certainly the
            // faster/cleaner option, rather than reimplementing the argument parsing
            // logic once again.

            template <typename = void> requires (!target::has_args)
            static std::function<Return(Target...)> cpp(
                PyObject* func,
                const DefaultValues& defaults
            ) {
                // non-variadic case can use a stack-allocated array
                return [func, &defaults](Target&&... args) -> Return {
                    PyObject* array[1 + target::size];
                    array[0] = nullptr;

                    []<size_t... Is>(
                        std::index_sequence<Is...>,
                        PyObject** array,
                        Target&&... args
                    ) {
                        (cpp_arg<Is>(array, std::forward<Target>(args)), ...);
                    }(
                        std::make_index_sequence<target::size>{},
                        array,
                        std::forward<Target>(args)...
                    );

                    PyObject* result = PyObject_Vectorcall(
                        func,
                        array + 1,
                        target::size | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        nullptr
                    );

                    for (size_t i = 1; i <= target::size; ++i) {
                        Py_XDECREF(array[i]);
                    }

                    if (result == nullptr) {
                        Exception::from_python();
                    }
                    return reinterpret_steal<Object>(result);
                };
            }

            template <typename = void> requires (target::has_args)
            static std::function<Return(Target...)> cpp(
                PyObject* func,
                const DefaultValues& defaults
            ) {
                // variadic case requires a heap-allocated array
                return [func, &defaults](Target&&... args) -> Return {
                    auto var_args = get_arg<target::args_index>(
                        std::forward<Target>(args)...
                    );
                    size_t nargs = target::size + var_args.size();
                    // if constexpr (!target::has_args) {
                    //     if (true_nargs > static_cast<Py_ssize_t>(target::size)) {
                    //         throw TypeError(
                    //             "expected at most " + std::to_string(target::size) +
                    //             " positional arguments, but received " +
                    //             std::to_string(true_nargs)
                    //         );    
                    //     }
                    // }

                    auto array = std::unique_ptr(
                        new PyObject*[1 + nargs],
                        [nargs](PyObject** array) {
                            for (size_t i = 1; i <= nargs; ++i) {
                                Py_XDECREF(array[i]);
                            }
                            delete[] array;
                        }
                    );
                    array[0] = nullptr;

                    []<size_t... Is>(
                        std::index_sequence<Is...>,
                        PyObject** array,
                        Target&&... args
                    ) {
                        (cpp_arg(array, std::forward<Target>(args)), ...);
                    }(
                        std::make_index_sequence<target::size>{},
                        array,
                        std::forward<Target>(args)...
                    );

                    size_t i = target::size;
                    try {
                        for (auto&& val : var_args) {
                            array[++i] = as_object(val).release().ptr();
                        }
                    } catch (...) {
                        for (size_t j = 1; j < i; ++j) {
                            Py_XDECREF(array[j]);
                        }
                        throw;
                    }

                    PyObject* result = PyObject_Vectorcall(
                        func,
                        array.get() + 1,
                        nargs | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        nullptr
                    );
                    if (result == nullptr) {
                        Exception::from_python();
                    }
                    return reinterpret_steal<Object>(result);
                };
            }

        };

        template <typename Dummy>
        struct Wrap<CallPolicy::keyword, Dummy> {
            static constexpr int flags = METH_FASTCALL | METH_KEYWORDS;

            template <size_t I, typename T>
            static Object python_arg(
                PyObject* const* args,
                Py_ssize_t nargs,
                PyObject* kwnames
            ) {
                // TODO: do complicated stuff here
                return {};
            }

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
                        Capsule* contents = get(capsule);
                        Py_ssize_t true_nargs = PyVectorcall_NARGS(nargs);

                        // create a tuple of decayed types to stabilize lifetimes
                        auto tuple = std::make_tuple(
                            python_arg<Is, typename target::template type<Is>>(
                                args,
                                true_nargs,
                                kwnames
                            )...
                        );

                        // forwarding from the tuple allows l/rvalues to bind correctly
                        return as_object(
                            std::apply(
                                [&contents](auto&... vals) {
                                    return contents->func(forward<Is>(vals)...);
                                },
                                tuple
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

            template <typename = void> requires (target::has_args && target::has_kwargs)
            static std::function<Return(Target...)> cpp(
                PyObject* func,
                const DefaultValues& defaults
            ) {
                
            }

            template <typename = void> requires (target::has_args)
            static std::function<Return(Target...)> cpp(
                PyObject* func,
                const DefaultValues& defaults
            ) {

            }

            template <typename = void> requires (target::has_kwargs)
            static std::function<Return(Target...)> cpp(
                PyObject* func,
                const DefaultValues& defaults
            ) {

            }

            template <typename = void> requires (!target::has_args && !target::has_kwargs)
            static std::function<Return(Target...)> cpp(
                PyObject* func,
                const DefaultValues& defaults
            ) {

            }

        };

    };

    std::shared_ptr<Capsule> contents;
    PyObject* m_ptr;

public:
    using Defaults = DefaultValues;

    template <typename... Args>
    static constexpr bool invocable = Arguments<Args...>::enable;

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
            name,
            std::forward<Func>(func),
            Defaults(std::forward<Values>(defaults)...)
        }),
        m_ptr(Capsule::to_python(contents))
    {}

    ~Function_() {
        Py_XDECREF(m_ptr);
    }

    template <typename... Args> requires (invocable<Args...>)
    static Return invoke_py(Handle func, Args&&... args) {
        // TODO: parse the arguments and replicate the logic from the Python wrappers
        // here.  This can be used internally to call the function from C++, using
        // Python-style arguments.  That's how I implement the keyword constructor for
        // py::Dict, etc.  I can also maybe bypass calling attr<> and just use a
        // GetAttr call + this to call built-in methods.  I can use the same base class
        // that Attr<> uses to avoid creating extra static strings.
    }

    /* Call an external C++ function that matches the target signature using the
    given defaults and Python-style arguments. */
    template <typename Func, typename... Args>
        requires (std::is_invocable_r_v<Return, Func, Target...> && invocable<Args...>)
    static Return invoke_cpp(const Defaults& defaults, Func&& func, Args&&... args) {
        return []<size_t... Is>(
            std::index_sequence<Is...>,
            const Defaults& defaults,
            Func&& func,
            Args&&... args
        ) {
            using sig = Signature<Args...>;
            if constexpr (sig::has_args && sig::has_kwargs) {
                auto var_kwargs = get_arg<sig::kwargs_index>(std::forward<Args>(args)...);
                if constexpr (!target::has_kwargs) {
                    validate_kwargs(
                        std::make_index_sequence<target::size>{},
                        var_kwargs
                    );
                }
                auto var_args = get_arg<sig::args_index>(std::forward<Args>(args)...);
                auto iter = var_args.begin();
                auto end = var_args.end();
                return func(
                    Arguments<Args...>::template unpack<Is, typename target::template type<Is>>(
                        defaults,
                        var_args.size(),
                        iter,
                        end,
                        var_kwargs,
                        std::forward<Args>(args)...
                    )...
                );
                if constexpr (!target::has_args) {
                    validate_args(iter, end);
                }
            } else if constexpr (sig::has_args) {
                auto var_args = get_arg<sig::args_index>(std::forward<Args>(args)...);
                auto iter = var_args.begin();
                auto end = var_args.end();
                return func(
                    Arguments<Args...>::template unpack<Is, typename target::template type<Is>>(
                        defaults,
                        var_args.size(),
                        iter,
                        end,
                        std::forward<Args>(args)...
                    )...
                );
                if constexpr (!target::has_args) {
                    validate_args(iter, end);
                }
            } else if constexpr (sig::has_kwargs) {
                auto var_kwargs = get_arg<sig::kwargs_index>(std::forward<Args>(args)...);
                if constexpr (!target::has_kwargs) {
                    validate_kwargs(
                        std::make_index_sequence<target::size>{},
                        var_kwargs
                    );
                }
                return func(
                    Arguments<Args...>::template unpack<Is, typename target::template type<Is>>(
                        defaults,
                        var_kwargs,
                        std::forward<Args>(args)...
                    )...
                );
            } else {
                return func(
                    Arguments<Args...>::template unpack<Is, typename target::template type<Is>>(
                        defaults,
                        std::forward<Args>(args)...
                    )...
                );
            }
        }(
            std::make_index_sequence<target::size>{},
            defaults,
            std::forward<Func>(func),
            std::forward<Args>(args)...
        );
    }

    /* Call the C++ function with the given arguments. */
    template <typename... Source> requires (invocable<Source...>)
    Return operator()(Source&&... args) const {
        return invoke_cpp(
            contents->defaults,
            contents->func,
            std::forward<Source>(args)...
        );
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
