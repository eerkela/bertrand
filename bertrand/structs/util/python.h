#ifndef BERTRAND_STRUCTS_UTIL_CONTAINER_H
#define BERTRAND_STRUCTS_UTIL_CONTAINER_H

#include <array>  // std::array
#include <cstddef>  // size_t
#include <cstdio>  // std::FILE, std::fopen
#include <deque>  // std::deque
#include <optional>  // std::optional
#include <sstream>  // std::ostringstream
#include <string>  // std::string
#include <string_view>  // std::string_view
#include <tuple>  // std::tuple
#include <type_traits>  // std::enable_if_t<>
#include <utility>  // std::pair, std::move, etc.
#include <valarray>  // std::valarray
#include <vector>  // std::vector
#include <Python.h>  // CPython API
#include "base.h"  // is_pyobject<>
#include "except.h"  // catch_python(), TypeError, KeyError, IndexError
#include "func.h"  // FuncTraits, identity
#include "iter.h"  // iter()
#include "pybind.h"  // namespace py::


// TODO: PyUnicode_AsUTF8() returns a pointer to a buffer that's internal to the string.
// This buffer is only valid as long as the string is alive.  We need to make sure that
// the string remains alive for as long as the buffer is in use.  This is not currently
// enforced, but it should be.


// TODO: All relevant constructors should be marked noexcept to encourage compiler
// optimizations.



// TODO: Indexing a Type object should call __class_getitem__(), if it exists.

// TODO: support pickling?  __reduce__(), __reduce_ex__(), __setstate__(), __getstate__(), etc.


/* NOTE: Python is great, but working with its C API is not.  As such, this file
 * contains a collection of wrappers around the CPython API that make it easier to work
 * with Python objects in C++.  The goal is to make the API more pythonic and less
 * error-prone, while still providing access to the full range of Python's
 * capabilities.
 *
 * Included in this file are:
 *  1.  RAII-based wrappers for PyObject* pointers that automatically handle reference
 *      counts and behave more like their Python counterparts.
 *  2.  Automatic logging of reference counts to ensure that they remain balanced over
 *      the entire program.
 *  3.  Wrappers for built-in Python functions, which are overloaded to handle STL
 *      types and conform to C++ conventions.
 *  4.  Generic programming support for mixed C++/Python objects, including automatic
 *      conversions for basic C++ types into their Python equivalents where possible.
 */


namespace bertrand {
namespace python {



////////////////////////////
////    CODE OBJECTS    ////
////////////////////////////


/* Code evaluation using the CPython API is very powerful, but also very confusing.
 * The following classes and methods attempt to simplify it, enabling the user to embed
 * Python as a scripting language directly within C++ applications with minimal effort.
 * Here's are some examples of how they work:
 *
 *     // return last expression
 *     python::Code foo("x + y");
 *     python::Int z = foo({{"x", 1}, {"y", 2}});
 *
 *     // extract result from context
 *     python::Code bar("z = x + y");
 *     python::Dict globals {{"x", 1}, {"y", 2}};
 *     bar(globals);
 *     python::Int z = globals["z"];
 *
 * Both give the same result for `z`, but show different ways of executing the code
 * object and extracting the result.  By default, when a code object is executed, it
 * returns the result of the last expression.  In the first case, this is what we're
 * looking for, so we can just capture the result of the call.  In the second example,
 * the assignment expression implicitly returns `None`, so we can't capture it
 * directly.  Instead, the assignment will insert an entry into the globals dictionary
 * that we can extract after the call.  Note that this second form always acts by
 * side-effect, modifying the global and local variables in place.
 *
 * The expressions above are relatively simple, but we can technically embed arbitrary
 * Python code using the same interface.  For example:
 *
 *     python::Code script({
 *         "class MyClass:",
 *         "    def __init__(self, x, y):",
 *         "        self.x = x",
 *         "        self.y = y",
 *         "",
 *         "def func(x, y):",
 *         "    return x + y",
 *         "",
 *         "obj = MyClass(1, 2)",
 *         "baz(obj.x, obj.y)"
 *     });
 *
 *     python::Dict globals;
 *     python::Int z = script(globals);
 *     python::Type MyClass = globals["MyClass"];
 *     python::Function func = globals["baz"];
 *
 * This allows us to embed full Python scripts directly into C++ code, and then export
 * the results back into a C++ environment.  This is very powerful, but should be used
 * with caution as it triggers the execution of arbitrary Python code.  This can lead
 * to security vulnerabilities if the input is not properly sanitized or quarantined.
 *
 * NOTE: using an initializer list to create a `Code` object is equivalent to
 * concatenating each string with a newline character and then compiling the result.
 * The initializer list syntax is intended to make it easier to define multi-line
 * Python scripts and make the resulting code more readable.  For example, these two
 * scripts are equivalent:
 *
 *     python::Code(
 *         "x = 1\n"
 *         "y = 2\n"
 *         "z = 3"
 *     )
 *
 *     python::Code f2({
 *         "x = 1",
 *         "y = 2",
 *         "z = 3"
 *     });
 */


/* An extension of python::Object that represents a Python code object. */
template <Ref ref = Ref::STEAL>
class Code : public Object<ref, Code> {
    using Base = Object<ref>;

    /* Concatenate a series of strings using initializer list syntax, inserting a
    newline character between each one. */
    template <typename T>
    static std::string accumulate(std::initializer_list<T> source) {
        std::string result;
        for (const auto& line : source) {
            if (result.empty()) {
                result += line;
            } else {
                result += '\n' + line;
            }
        }
        return result;
    }

public:
    using Base::Base;
    using Base::operator=;

    /* Implicitly convert a PyObject* into a python::Code object. */
    Code(PyObject* obj) : Base([&] {
        if (obj == nullptr || !PyCode_Check(obj)) {
            throw ValueError("expected a code object");
        }
        return obj;
    }()) {}

    /* Implicitly convert a PyCodeObject* into a python::Code object. */
    Code(PyCodeObject* obj) : Base([&] {
        if (obj == nullptr) {
            throw ValueError("expected a code object");
        }
        return reinterpret_cast<PyObject*>(obj);
    }()) {}

    /* Parse and compile a source string into a Python code object.  The filename is
    used in to construct the code object and may appear in tracebacks or exception
    messages.  The mode is used to constrain the code that can be compiled, and must be
    one of `Py_eval_input`, `Py_file_input`, or `Py_single_input` for multiline
    strings, file contents, and single-line, REPL-style statements respectively. */
    explicit Code(
        const char* source,
        const char* filename = nullptr,
        int mode = Py_eval_input
    ) {
        static_assert(
            ref != Ref::BORROW,
            "Cannot construct a non-owning reference to a new code object"
        );

        if (mode != Py_file_input && mode != Py_eval_input && mode != Py_single_input) {
            std::ostringstream msg;
            msg << "invalid compilation mode: " << mode << " <- must be one of ";
            msg << "Py_file_input, Py_eval_input, or Py_single_input";
            throw ValueError(msg.str());
        }

        if (filename == nullptr) {
            filename = "<anonymous file>";
        }

        this->obj = Py_CompileString(source, filename, mode);
        if (this->obj == nullptr) {
            throw catch_python();
        }
    }

    /* Parse and compile a sequence of source strings into a Python code object using
    an initializer list.  This concatenates each string and inserts a newline character
    between them, making it easier to define multi-line Python scripts.  Otherwise, it
    behaves just like compiling from a single string instead. */
    explicit Code(
        std::initializer_list<const char*> source,
        const char* filename = nullptr,
        int mode = Py_eval_input
    ) : Code(accumulate(source).c_str(), filename, mode)
    {}

    /* See const char* overload. */
    explicit Code(
        const char* source,
        const std::string& filename,
        int mode = Py_eval_input
    ) : Code(source, filename.c_str(), mode)
    {}

    /* See const char* overload. */
    explicit Code(
        std::initializer_list<const char*> source,
        const std::string& filename,
        int mode = Py_eval_input
    ) : Code(accumulate(source).c_str(), filename.c_str(), mode)
    {}

    /* See const char* overload. */
    explicit Code(
        const char* source,
        const std::string_view& filename,
        int mode = Py_eval_input
    ) : Code(source, filename.data(), mode)
    {}

    /* See const char* overload. */
    explicit Code(
        std::initializer_list<const char*> source,
        const std::string_view& filename,
        int mode = Py_eval_input
    ) : Code(accumulate(source).c_str(), filename.data(), mode)
    {}

    /* See const char* overload. */
    explicit Code(
        const std::string& source,
        const char* filename = nullptr,
        int mode = Py_eval_input
    ) : Code(source.c_str(), filename, mode)
    {}

    /* See const char* overload. */
    explicit Code(
        std::initializer_list<std::string> source,
        const char* filename = nullptr,
        int mode = Py_eval_input
    ) : Code(accumulate(source).c_str(), filename, mode)
    {}

    /* See const char* overload. */
    explicit Code(
        const std::string& source,
        const std::string& filename,
        int mode = Py_eval_input
    ) : Code(source.c_str(), filename.c_str(), mode)
    {}

    /* See const char* overload. */
    explicit Code(
        std::initializer_list<std::string> source,
        const std::string& filename,
        int mode = Py_eval_input
    ) : Code(accumulate(source).c_str(), filename.c_str(), mode)
    {}

    /* See const char* overload. */
    explicit Code(
        const std::string_view& source,
        const char* filename = nullptr,
        int mode = Py_eval_input
    ) : Code(source.data(), filename, mode)
    {}

    /* See const char* overload. */
    explicit Code(
        std::initializer_list<std::string_view> source,
        const char* filename = nullptr,
        int mode = Py_eval_input
    ) : Code(accumulate(source).c_str(), filename, mode)
    {}

    /* See const char* overload. */
    explicit Code(
        const std::string_view& source,
        const std::string& filename,
        int mode = Py_eval_input
    ) : Code(source.data(), filename.c_str(), mode)
    {}

    /* See const char* overload. */
    explicit Code(
        std::initializer_list<std::string_view> source,
        const std::string& filename,
        int mode = Py_eval_input
    ) : Code(accumulate(source).c_str(), filename.c_str(), mode)
    {}

    /* See const char* overload. */
    explicit Code(
        const std::string_view& source,
        const std::string_view& filename,
        int mode = Py_eval_input
    ) : Code(source.data(), filename.data(), mode)
    {}

    /* See const char* overload. */
    explicit Code(
        std::initializer_list<std::string_view> source,
        const std::string_view& filename,
        int mode = Py_eval_input
    ) : Code(accumulate(source).c_str(), filename.data(), mode)
    {}

    /* Implicitly convert a python::List into a PyCodeObject* pointer. */
    inline operator PyCodeObject*() const noexcept {
        return reinterpret_cast<PyCodeObject*>(this->obj);
    }

    ////////////////////////////////
    ////    PyCode_* METHODS    ////
    ////////////////////////////////

    /* Execute the code object with the given local and global variables. */
    inline Object<Ref::STEAL> operator()(
        Dict<Ref::NEW> globals = {},
        Dict<Ref::NEW> locals = {}
    ) const {
        PyObject* result = PyEval_EvalCode(this->obj, globals, locals);
        if (result == nullptr) {
            throw catch_python();
        }
        return result;
    }

    /* Get the name of the file from which the code was compiled. */
    inline std::string file() const {
        return {reinterpret_cast<PyCodeObject*>(this->obj)->co_filename};
    }

    /* Get the function's base name. */
    inline std::string name() const {
        return {reinterpret_cast<PyCodeObject*>(this->obj)->co_name};
    }

    /* Get the function's qualified name. */
    inline std::string qualname() const {
        return {reinterpret_cast<PyCodeObject*>(this->obj)->co_qualname};
    }

    /* Get the first line number of the function. */
    inline Py_ssize_t line_number() const noexcept {
        return reinterpret_cast<PyCodeObject*>(this->obj)->co_firstlineno;
    }

    /* Get the total number of positional arguments for the function, including
    positional-only arguments and those with default values (but not keyword-only). */
    inline Py_ssize_t n_args() const noexcept {
        return reinterpret_cast<PyCodeObject*>(this->obj)->co_argcount;
    }

    /* Get the number of positional-only arguments for the function, including those
    with default values.  Does not include variable positional or keyword arguments. */
    inline Py_ssize_t n_positional() const noexcept {
        return reinterpret_cast<PyCodeObject*>(this->obj)->co_posonlyargcount;
    }

    /* Get the number of keyword-only arguments for the function, including those with
    default values.  Does not include positional-only or variable positional/keyword
    arguments. */
    inline Py_ssize_t n_keyword() const noexcept {
        return reinterpret_cast<PyCodeObject*>(this->obj)->co_kwonlyargcount;
    }

    /* Get the number of local variables used by the function (including all
    parameters). */
    inline Py_ssize_t n_locals() const noexcept {
        return reinterpret_cast<PyCodeObject*>(this->obj)->co_nlocals;
    }

    /* Get a tuple containing the names of the local variables in the function,
    starting with parameter names. */
    inline Tuple<Ref::BORROW> locals() const {
        return {reinterpret_cast<PyCodeObject*>(this->obj)->co_varnames};
    }

    /* Get a tuple containing the names of local variables that are referenced by
    nested functions within this function (i.e. those that are stored in a PyCell). */
    inline Tuple<Ref::BORROW> cellvars() const {
        return {reinterpret_cast<PyCodeObject*>(this->obj)->co_cellvars};
    }

    /* Get a tuple containing the anmes of free variables in the function (i.e.
    those that are not stored in a PyCell). */
    inline Tuple<Ref::BORROW> freevars() const {
        return {reinterpret_cast<PyCodeObject*>(this->obj)->co_freevars};
    }

    /* Get the required stack space for the code object. */
    inline Py_ssize_t stack_size() const noexcept {
        return reinterpret_cast<PyCodeObject*>(this->obj)->co_stacksize;
    }

    /* Get the bytecode buffer representing the sequence of instructions in the
    function. */
    inline const char* bytecode() const {
        return {reinterpret_cast<PyCodeObject*>(this->obj)->co_code};
    }

    /* Get a tuple containing the literals used by the bytecode in the function. */
    inline Tuple<Ref::BORROW> constants() const {
        return {reinterpret_cast<PyCodeObject*>(this->obj)->co_consts};
    }

    /* Get a tuple containing the names used by the bytecode in the function. */
    inline Tuple<Ref::BORROW> names() const {
        return {reinterpret_cast<PyCodeObject*>(this->obj)->co_names};
    }

    /* Get an integer encoding flags for the Python interpreter. */
    inline int flags() const noexcept {
        return reinterpret_cast<PyCodeObject*>(this->obj)->co_flags;
    }

};


/* An extension of python::Object that represents a bytecode execution frame. */
template <Ref ref = Ref::STEAL>
class Frame : public Object<ref, Frame> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;

    /* Implicitly convert a PyObject* into a python::Frame. */
    Frame(PyObject* obj) : Base([&] {
        if (obj == nullptr || !PyFrame_Check(obj)) {
            throw TypeError("expected a frame");
        }
        return obj;
    }()) {}

    /* Implicitly convert a PyFrameObject* into a python::Frame. */
    Frame(PyFrameObject* frame) : Base([&] {
        if (frame == nullptr) {
            throw TypeError("expected a frame");
        }
        return reinterpret_cast<PyObject*>(frame);
    }()) {}

    /* Implicitly convert a python::Frame into a PyFrameObject* pointer. */
    inline operator PyFrameObject*() const noexcept {
        return reinterpret_cast<PyFrameObject*>(this->obj);
    }

    /////////////////////////////////
    ////    PyFrame_* METHODS    ////
    /////////////////////////////////

    /* Get the current execution frame. */
    inline static Frame<Ref::BORROW> current() {
        PyObject* frame = reinterpret_cast<PyObject*>(PyEval_GetFrame());
        if (frame == nullptr) {
            throw RuntimeError("no frame is currently executing");
        }
        return {frame};
    }

    /* Execute the code object stored within the frame using its current context,
    interpreting bytecode and executing instructions as needed until it reaches the
    end of its code path.  This is the main entry point for all Python code evaluation,
    and is used behind the scenes whenever a Python program is run.   */
    inline Object<Ref::STEAL> operator()() const {
        PyObject* result = PyEval_EvalFrame(static_cast<PyFrameObject*>(*this));
        if (result == nullptr) {
            throw catch_python();
        }
        return result;
    }

    /* Get the line number that the frame is currently executing. */
    inline int line_number() const noexcept {
        return PyFrame_GetLineNumber(static_cast<PyFrameObject*>(*this));
    }

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 9)

        /* Get the next outer frame from this one. */
        inline Frame<Ref::STEAL> back() const {
            return {PyFrame_GetBack(static_cast<PyFrameObject*>(*this))};
        }

        /* Get the code object associated with this frame. */
        inline Code<Ref::STEAL> code() const {
            return {PyFrame_GetCode(static_cast<PyFrameObject*>(*this))};
        }

    #endif

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 11)

        /* Get the frame's builtin namespace. */
        inline Dict<Ref::STEAL> builtins() const {
            return {PyFrame_GetBuiltins(static_cast<PyFrameObject*>(*this))};
        }

        /* Get the frame's globals namespace. */
        inline Dict<Ref::STEAL> globals() const {
            return {PyFrame_GetGlobals(static_cast<PyFrameObject*>(*this))};
        }

        /* Get the frame's locals namespace. */
        inline Dict<Ref::STEAL> locals() const {
            return {PyFrame_GetLocals(static_cast<PyFrameObject*>(*this))};
        }

        /* Get the generator, coroutine, or async generator that owns this frame, or
        nullopt if this frame is not owned by a generator. */
        inline std::optional<Object<Ref::STEAL>> generator() const {
            PyObject* result = PyFrame_GetGenerator(static_cast<PyFrameObject*>(*this));
            if (result == nullptr) {
                return std::nullopt;
            } else {
                return std::make_optional(Object<Ref::STEAL>(result));
            }
        }

        /* Get the "precise instruction" of the frame object, which is an index into
        the bytecode of the last instruction executed by the frame's code object. */
        inline int last_instruction() const noexcept {
            return PyFrame_GetLasti(static_cast<PyFrameObject*>(*this));
        }

    #endif

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12)

        /* Get a named variable from the frame's context.  Can raise if the variable is
        not present in the frame. */
        inline Object<Ref::STEAL> get(PyObject* name) const {
            PyObject* result = PyFrame_GetVar(static_cast<PyFrameObject*>(*this), name);
            if (result == nullptr) {
                throw catch_python();
            }
            return {result};
        }

        /* Get a named variable from the frame's context.  Can raise if the variable is
        not present in the frame. */
        inline Object<Ref::STEAL> get(const char* name) const {
            PyObject* result = PyFrame_GetVarString(
                static_cast<PyFrameObject*>(*this),
                name
            );
            if (result == nullptr) {
                throw catch_python();
            }
            return {result};
        }

        /* Get a named variable from the frame's context.  Can raise if the variable is
        not present in the frame. */
        inline Object<Ref::STEAL> get(const std::string& name) const {
            return get(name.c_str());
        }

        /* Get a named variable from the frame's context.  Can raise if the variable is
        not present in the frame. */
        inline Object<Ref::STEAL> get(const std::string_view& name) const {
            return get(name.data());
        }

    #endif

};


/* An extension of python::Object that represents a Python function. */
template <Ref ref = Ref::STEAL>
class Function : public Object<ref, Function> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;

    /* Implicitly convert a PyObject* into a python::Function. */
    Function(PyObject* obj) : Base([&] {
        if (obj == nullptr || !PyFunction_Check(obj)) {
            throw TypeError("expected a function");
        }
        return obj;
    }()) {}

    /* Implicitly convert a PyFunctionObject* into a python::Function. */
    Function(PyFunctionObject* func) : Base([&] {
        if (func == nullptr) {
            throw TypeError("expected a function");
        }
        return reinterpret_cast<PyObject*>(func);
    }()) {}

    /* Create a new Python function from a code object and a globals dictionary. */
    explicit Function(PyCodeObject* code, Dict<Ref::NEW> globals) {
        static_assert(
            ref != Ref::BORROW,
            "Cannot construct a non-owning reference to a new function object"
        );
        this->obj = PyFunction_New(code, globals);
        if (this->obj == nullptr) {
            throw catch_python();
        }
    }

    /* Create a new Python function from a code object, globals dictionary, and a
    qualified name. */
    explicit Function(
        PyCodeObject* code,
        Dict<Ref::NEW> globals,
        String<Ref::NEW> qualname
    ) {
        static_assert(
            ref != Ref::BORROW,
            "Cannot construct a non-owning reference to a new function object"
        );
        this->obj = PyFunction_NewWithQualName(code, globals, qualname);
        if (this->obj == nullptr) {
            throw catch_python();
        }
    }

    /* Implicitly convert a python::Function into a PyFunctionObject*. */
    inline operator PyFunctionObject*() const noexcept {
        return reinterpret_cast<PyFunctionObject*>(this->obj);
    }

    ////////////////////////////////////////
    ////    PyFunction_* API METHODS    ////
    ////////////////////////////////////////

    /* Get the name of the file from which the code was compiled. */
    inline std::string filename() const {
        return code().filename();
    }

    /* Get the module that the function is defined in. */
    inline std::optional<Module<Ref::BORROW>> module_() const {
        PyObject* mod = PyFunction_GetModule(this->obj);
        if (mod == nullptr) {
            return std::nullopt;
        } else {
            return std::make_optional(Module<Ref::BORROW>(module));
        }
    }

    /* Get the first line number of the function. */
    inline size_t line_number() const noexcept {
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

    /* Get the function's code object. */
    inline Code<Ref::BORROW> code() const noexcept {
        return Code<Ref::BORROW>(PyFunction_GetCode(this->obj));
    }

    /* Get the closure associated with the function.  This is a tuple of cell objects
    containing data captured by the function. */
    inline Tuple<Ref::BORROW> closure() const noexcept {
        PyObject* closure = PyFunction_GetClosure(this->obj);
        if (closure == nullptr) {
            return {};  // TODO: returning a default-constructed Tuple with Ref::BORROW is not allowed.
        } else {
            return {closure};
        }
    }

    /* Set the closure associated with the function.  Input must be Py_None or a
    tuple. */
    inline void closure(PyObject* closure) {
        if (PyFunction_SetClosure(this->obj, closure)) {
            throw catch_python();
        }
    }

    /* Get the globals dictionary associated with the function object. */
    inline Dict<Ref::BORROW> globals() const noexcept {
        return Dict<Ref::BORROW>(PyFunction_GetGlobals(this->obj));
    }

    /* Get the total number of positional arguments for the function, including
    positional-only arguments and those with default values (but not keyword-only). */
    inline size_t n_args() const noexcept {
        return code().n_args();
    }

    /* Get the number of positional-only arguments for the function, including those
    with default values.  Does not include variable positional or keyword arguments. */
    inline size_t n_positional() const noexcept {
        return code().n_positional();
    }

    /* Get the number of keyword-only arguments for the function, including those with
    default values.  Does not include positional-only or variable positional/keyword
    arguments. */
    inline size_t n_keyword() const noexcept {
        return code().n_keyword();
    }

    /* Get the annotations for the function object.  This is a mutable dictionary or
    null if no annotations are present. */
    inline Dict<Ref::BORROW> annotations() const noexcept {
        PyObject* annotations = PyFunction_GetAnnotations(this->obj);
        if (annotations == nullptr) {
            return {};
        } else {
            return {annotations};
        }
    }

    /* Set the annotations for the function object.  Input must be Py_None or a
    dictionary. */
    inline void annotations(Dict<Ref::NEW> annotations) {
        if (PyFunction_SetAnnotations(this->obj, annotations)) {
            throw catch_python();
        }
    }

    /* Get the default values for the function's arguments. */
    inline Tuple<Ref::BORROW> defaults() const noexcept {
        PyObject* defaults = PyFunction_GetDefaults(this->obj);
        if (defaults == nullptr) {
            return {};  // TODO: returning a default-constructed Tuple with Ref::BORROW is not allowed.
        } else {
            return {defaults};
        }
    }

    /* Set the default values for the function's arguments.  Input must be Py_None or
    a tuple. */
    inline void defaults(Dict<Ref::NEW> defaults) {
        if (PyFunction_SetDefaults(this->obj, defaults)) {
            throw catch_python();
        }
    }

};


/* An extension of python::Object that represents a bound Python method. */
template <Ref ref = Ref::STEAL>
class Method : public Object<ref, Method> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;

    /* Implicitly convert a PyObject* into a python::Method. */
    Method(PyObject* obj) : Base([&] {
        if (obj == nullptr || !PyMethod_Check(obj)) {
            std::ostringstream msg;
            msg << "expected a method, got " << repr(obj);
            throw TypeError(msg.str());
        }
        return obj;
    }()) {}

    /* Create a new method by binding an object to a function as an implicit `self`
    argument. */
    explicit Method(PyObject* self, PyObject* function) {
        static_assert(
            ref != Ref::BORROW,
            "Cannot construct a non-owning reference to a new method"
        );
        if (self == nullptr) {
            throw TypeError("self object must not be null");
        }
        this->obj = PyMethod_New(function, self);
        if (this->obj == nullptr) {
            throw catch_python();
        }
    }

    // /* Wrap a function as a method descriptor.  When attached to a class, the
    // descriptor passes `self` as the first argument to the function. */
    // explicit Method(
    //     PyTypeObject* type,
    //     const char* name,
    //     PyCFunction function,
    //     int flags = METH_VARARGS | METH_KEYWORDS,
    //     const char* doc = nullptr
    // ) {
    //     static_assert(
    //         ref != Ref::BORROW,
    //         "Cannot construct a non-owning reference to a new method"
    //     );
    //     PyMethodDef method = {name, function, flags, doc};
    //     this->obj = PyDescr_NewMethod(type, &method);
    //     if (this->obj == nullptr) {
    //         throw catch_python();
    //     }
    // }

    //////////////////////////////////////
    ////    PyMethod_* API METHODS    ////
    //////////////////////////////////////

    /* Get the function object associated with the method. */
    inline Function<Ref::BORROW> function() const {
        return Function<Ref::BORROW>(PyMethod_GET_FUNCTION(this->obj));
    }

    /* Get the instance to which the method is bound. */
    inline Object<Ref::BORROW> self() const {
        return Object<Ref::BORROW>(PyMethod_GET_SELF(this->obj));
    }

    /* Get the name of the file from which the code was compiled. */
    inline std::string filename() const {
        return function().filename();
    }

    /* Get the first line number of the method. */
    inline size_t line_number() const {
        return function().line_number();
    }

    /* Get the module that the method is defined in. */
    inline std::optional<Object<Ref::BORROW>> module_() const {
        return function().module_();
    }

    /* Get the method's base name. */
    inline std::string name() const {
        return function().name();
    }

    /* Get the method's qualified name. */
    inline std::string qualname() const {
        return function().qualname();
    }

    /* Get the code object wrapped by this method. */
    inline Code<Ref::BORROW> code() const {
        return function().code();
    }

    /* Get the closure associated with the method.  This is a tuple of cell objects
    containing data captured by the method. */
    inline std::optional<Tuple<Ref::BORROW>> closure() const {
        return function().closure();
    }

    /* Set the closure associated with the method.  Input must be Py_None or a tuple. */
    inline void closure(PyObject* closure) {
        function().closure(closure);
    }

    /* Get the globals dictionary associated with the method object. */
    inline Dict<Ref::BORROW> globals() const {
        return function().globals();
    }

    /* Get the total number of positional arguments for the method, including
    positional-only arguments and those with default values (but not keyword-only). */
    inline size_t n_args() const {
        return function().n_args();
    }

    /* Get the number of positional-only arguments for the method, including those
    with default values.  Does not include variable positional or keyword arguments. */
    inline size_t n_positional() const {
        return function().n_positional();
    }

    /* Get the number of keyword-only arguments for the method, including those with
    default values.  Does not include positional-only or variable positional/keyword
    arguments. */
    inline size_t n_keyword() const {
        return function().n_keyword();
    }

    /* Get the type annotations for each argument.  This is a mutable dictionary or
    null if no annotations are present. */
    inline Dict<Ref::BORROW> annotations() const {
        return function().annotations();
    }

    /* Set the annotations for the method object.  Input must be Py_None or a
    dictionary. */
    inline void annotations(Dict<Ref::NEW> annotations) {
        function().annotations(annotations);
    }

    /* Get the default values for the method's arguments. */
    inline std::optional<Tuple<Ref::BORROW>> defaults() const {
        return function().defaults();
    }

    /* Set the default values for the method's arguments.  Input must be Py_None or a
    tuple. */
    inline void defaults(Dict<Ref::NEW> defaults) {
        function().defaults(defaults);
    }

};


// TODO: descriptor (PyDescr)

// Descriptor::property()
// Descriptor::classmethod()
// Descriptor::staticmethod()


/* Evaluate an arbitrary Python statement encoded as a string. */
inline Object eval(
    const char* statement,
    Dict<Ref::NEW> globals = {},
    Dict<Ref::NEW> locals = {}
) {
    PyObject* result = PyRun_String(statement, Py_eval_input, globals, locals);
    if (result == nullptr) {
        throw catch_python();
    }
    return result;
}


/* Evaluate an arbitrary Python statement encoded as a string. */
inline Object eval(
    const std::string& statement,
    Dict<Ref::NEW> globals = {},
    Dict<Ref::NEW> locals = {}
) {
    PyObject* result = PyRun_String(statement.c_str(), Py_eval_input, globals, locals);
    if (result == nullptr) {
        throw catch_python();
    }
    return result;
}


/* Evaluate an arbitrary Python statement encoded as a string. */
inline Object eval(
    const std::string_view& statement,
    Dict<Ref::NEW> globals = {},
    Dict<Ref::NEW> locals = {}
) {
    PyObject* result = PyRun_String(statement.data(), Py_eval_input, globals, locals);
    if (result == nullptr) {
        throw catch_python();
    }
    return result;
}


/* Launch a subinterpreter to execute a python script stored in a .py file. */
void run(const char* filename) {
    // NOTE: Python recommends that on windows platforms, we open the file in binary
    // mode to avoid issues with newline characters.

    #if defined(_WIN32) || defined(_WIN64)
        std::FILE* file = std::fopen(filename.c_str(), "rb");
    #else
        std::FILE* file = std::fopen(filename.c_str(), "r");
    #endif

    if (file == nullptr) {
        std::ostringstream msg;
        msg << "could not open file '" << filename << "'";
        throw FileNotFoundError(msg.str());
    }

    // NOTE: PyRun_SimpleFileEx() launches an interpreter, executes the file, and then
    // closes the file connection automatically.  It returns 0 on success and -1 on
    // failure, with no way of recovering the original error message if one is raised.

    if (PyRun_SimpleFileEx(file, filename.c_str(), 1)) {
        std::ostringstream msg;
        msg << "error occurred while running file '" << filename << "'";
        throw RuntimeError(msg.str());
    }
}


/* Launch a subinterpreter to execute a python script stored in a .py file. */
inline void run(const std::string& filename) {
    run(filename.c_str());
}


/* Launch a subinterpreter to execute a python script stored in a .py file. */
inline void run(const std::string_view& filename) {
    run(filename.data());
}


/////////////////////
////    OTHER    ////
/////////////////////


/* A wrapper around a fast Python sequence (list or tuple) that manages reference
counts and simplifies access. */
template <Ref ref = Ref::STEAL>
class FastSequence : public Object<ref, FastSequence> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;

    /* Construct a PySequence from an iterable or other sequence. */
    FastSequence(PyObject* obj) : Base(obj) {
        if (!PyTuple_Check(obj) && !PyList_Check(obj)) {
            throw TypeError("expected a tuple or list");
        }
    }

    /////////////////////////////////////////
    ////    PySequence_Fast_* METHODS    ////
    /////////////////////////////////////////

    /* Get the size of the sequence. */
    inline size_t size() const {
        return static_cast<size_t>(PySequence_Fast_GET_SIZE(this->obj));
    }

    /* Get underlying PyObject* array. */
    inline PyObject** data() const {
        return PySequence_Fast_ITEMS(this->obj);
    }

    /* Directly get an item within the sequence without boundschecking.  Returns a
    borrowed reference. */
    inline PyObject* GET_ITEM(Py_ssize_t index) const {
        return PySequence_Fast_GET_ITEM(this->obj, index);
    }

    /* Get the value at a particular index of the sequence.  Returns a borrowed
    reference. */
    inline PyObject* operator[](size_t index) const {
        if (index >= size()) {
            throw IndexError("index out of range");
        }
        return GET_ITEM(index);
    }

};


}  // namespace python


/* A trait that controls which C++ types are passed through the sequence() helper
without modification.  These must be vector or array types that support a definite
`.size()` method as well as integer-based indexing via the `[]` operator.  If a type
does not appear here, then it is converted into a std::vector instead. */
template <typename T>
struct SequenceFilter : std::false_type {};

template <typename T, typename Alloc>
struct SequenceFilter<std::vector<T, Alloc>> : std::true_type {};

template <typename T, size_t N> 
struct SequenceFilter<std::array<T, N>> : std::true_type {};

template <typename T, typename Alloc>
struct SequenceFilter<std::deque<T, Alloc>> : std::true_type {};

template <>
struct SequenceFilter<std::string> : std::true_type {};
template <>
struct SequenceFilter<std::wstring> : std::true_type {};
template <>
struct SequenceFilter<std::u16string> : std::true_type {};
template <>
struct SequenceFilter<std::u32string> : std::true_type {};

template <>
struct SequenceFilter<std::string_view> : std::true_type {};
template <>
struct SequenceFilter<std::wstring_view> : std::true_type {};
template <>
struct SequenceFilter<std::u16string_view> : std::true_type {};
template <>
struct SequenceFilter<std::u32string_view> : std::true_type {};

template <typename T>
struct SequenceFilter<std::valarray<T>> : std::true_type {};

#if __cplusplux >= 202002L  // C++20 or later

    template <typename T>
    struct SequenceFilter<std::span<T>> : std::true_type {};

    template <>
    struct SequenceFilter<std::u8string> : std::true_type {};

    template <>
    struct SequenceFilter<std::u8string_view> : std::true_type {};

#endif



/* Unpack an arbitrary Python iterable or C++ container into a sequence that supports
random access.  If the input already supports these, then it is returned directly. */
template <typename Iterable>
auto sequence(Iterable&& iterable) {

    if constexpr (is_pyobject<Iterable>) {
        PyObject* seq = PySequence_Fast(iterable, "expected a sequence");
        return python::FastSequence<python::Ref::STEAL>(seq);

    } else {
        using Traits = ContainerTraits<Iterable>;
        static_assert(Traits::forward_iterable, "container must be forward iterable");

        // if the container already supports random access, then return it directly
        if constexpr (
            SequenceFilter<
                std::remove_cv_t<
                    std::remove_reference_t<Iterable>
                >
            >::value
        ) {
            return std::forward<Iterable>(iterable);
        } else {
            auto proxy = iter(iterable);
            auto it = proxy.begin();
            auto end = proxy.end();
            using Deref = decltype(*std::declval<decltype(it)>());
            return std::vector<Deref>(it, end);
        }
    }
}


}  // namespace bertrand


#undef PYTHON_SIMPLIFIED_ERROR_STATE
#endif  // BERTRAND_STRUCTS_UTIL_CONTAINER_H
