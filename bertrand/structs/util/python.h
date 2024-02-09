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


// /* Reference counting protocols for correctly managing PyObject* lifetimes. */
// enum class Ref {
//     NEW,    // increment on construction, decrement on destruction.
//     STEAL,  // decrement on destruction.  Assumes ownership over object.
//     BORROW  // do not modify refcount.  Object is assumed to outlive the wrapper.
// };


/* Enum to simplify iterator direction templates. */
enum class Direction {
    FORWARD,
    REVERSE
};


////////////////////////////////////
////    FORWARD DECLARATIONS    ////
////////////////////////////////////


class Handle;
class Object;
class Type;
class Function;
class Method;
class ClassMethod;
class Module;
class Frame;
class Code;
class Bool;
class Int;
class Float;
class Complex;
class Slice;
class Tuple;
class List;
class Set;
class Dict;
class FastSequence;
class String;


///////////////////////////////////
////    FUNDAMENTAL OBJECTS    ////
///////////////////////////////////


namespace traits {

    /* Check whether the templated type refers to an object or one of its subclasses. */
    template <typename T>
    static constexpr bool is_object = (
        std::is_base_of_v<Object<Ref::NEW>, T> ||
        std::is_base_of_v<Object<Ref::STEAL>, T> ||
        std::is_base_of_v<Object<Ref::BORROW>, T>
    );

}


/* A smart wrapper around a PyObject* pointer that automatically manages reference
counts according to a templated reference protocol.

python::Objects can be used in a variety of ways, and are designed to make interacting
with the CPython API as simple as possible from a C++ perspective.  In most cases, they
can be used identically to their Python counterparts, with the same semantics in both
languages.  They are both implicitly constructible from and convertible to PyObject*
pointers, allowing them to be passed transparently to most Python C API functions,
which are exposed in simplified form as ordinary member methods.  They can also be
iterated over, indexed, and called as if they were Python objects, with identical
semantics to normal Python.  They can also be added, subtracted, multiplied, and so on,
all delegating to the appropriate Python special methods.

What's more, Objects can be specified as parameter types in C++ functions, meaning
they can directly replace PyObject* pointers in most cases.  This has the advantage of
automatically applying any implicit conversions that are available for the given
inputs, making it possible to pass C++ types directly to Python functions without any
explicit conversions.  In combination with automatic reference counting, this means
that users should be able to write fully generic C++ code that looks and feels almost
exactly like standard Python, without having to worry about manual reference counting
or low-level memory management.

The reference protocol is specified as a template parameter, and can be one of three
values:
    1.  Ref::NEW: The wrapper increments the reference count on construction and
        decrements it on destruction.  This is the most common behavior for function
        parameter types, as it both allows Python objects to be passed to C++ functions
        without altering their net reference count, and allows the wrapper to construct
        temporary objects from C++ inputs while respecting automatic reference
        counting.
    2.  Ref::STEAL: The wrapper does not modify the reference count on construction,
        but decrements it on destruction.  This is the most common behavior for return
        types, as it allows the wrapper to transfer ownership of the underlying
        PyObject* to the caller without having to worry about reference counting.
        As such, it is also the default behavior for the wrapper, allowing users to
        omit the template parameter when capturing the result of a function call.
    3.  Ref::BORROW: The wrapper does not modify the reference count at all.  This is
        the highest performance option, but can be unsafe if the wrapper outlives the
        underlying PyObject*.  It also means that C++ inputs are not allowed, since
        the conversion would require a new reference to be created.  As such, it is only
        recommended for simple functions that do not need to own their inputs, or for
        temporary references that are guaranteed to outlive the wrapper.

Subclasses of this type can be used to wrap specific Python types, such as built-in
integers, floats, and strings, as well as containers like lists, tuples, and
dictionaries.  These subclasses provide additional methods and operators that are
particular to the given type, and use stricter conversion rules that allow for greater
type safety.  For C++ objects, this will often translate into compile-time errors if
the wrong type is used, ensuring correctness at the C++ level.  For Python objects, it
often translates into a null check followed by an isinstance() call against the
specified type, which are executed at runtime.  In the case of built-in types, this can
be significantly faster than the equivalent Python code, thanks to the use of lower
level API calls rather than isinstance() directly.
*/
// template <Ref ref = Ref::STEAL, typename _Derived = void>
struct Object : public py::object {


    /////////////////////////////////
    ////    SEQUENCE PROTOCOL    ////
    /////////////////////////////////

    /* Concatenate the object with another sequence.  Can throw if the object does not
    implement the `sq_concat` slot of the sequence protocol. */
    template <typename T>
    inline Object<Ref::STEAL> concat(T&& other) const {
        PyObject* result = PySequence_Concat(obj, as_object(std::forward<T>(other)));
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    /* Concatenate the object with another sequence in-place.  Can throw if the object
    does not implement the `sq_concat` slot of the sequence protocol. */
    template <typename T>
    inline Object& inplace_concat(T&& other) {
        static_assert(
            ref != Ref::BORROW,
            "cannot call inplace_concat() on a borrowed reference"
        );
        PyObject* result = PySequence_InPlaceConcat(
            obj, as_object(std::forward<T>(other))
        );
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }

    /* Repeat the object a given number of times.  Can throw if the object does not
    implement the `sq_repeat` slot of the sequence protocol. */
    inline Object<Ref::STEAL> repeat(Py_ssize_t count) const {
        PyObject* result = PySequence_Repeat(obj, count);
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    /* Repeat the object a given number of times in-place.  Can throw if the object does
    not implement the `sq_repeat` slot of the sequence protocol. */
    inline Object& inplace_repeat(Py_ssize_t count) {
        static_assert(
            ref != Ref::BORROW,
            "cannot call inplace_repeat() on a borrowed reference"
        );
        PyObject* result = PySequence_InPlaceRepeat(obj, count);
        if (result == nullptr) {
            throw catch_python();
        }
        PyObject* prev = obj;
        obj = result;
        Py_DECREF(prev);
        return *this;
    }


};


///////////////////////////////
////    TYPES & MODULES    ////
///////////////////////////////


/* An extension of python::Object that represents a Python module. */
template <Ref ref = Ref::STEAL>
class Module : public Object<ref, Module> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;

    /* Implicitly convert a PyObject* into a python::Module. */
    Module(PyObject* obj) : Base([&] {
        if (obj == nullptr || !PyModule_Check(obj)) {
            std::ostringstream msg;
            msg << "expected a module, got " << repr(obj);
            throw TypeError(msg.str());
        }
        return obj;
    }()) {}

    /* Create a Python module from a PyModuleDef* struct. */
    explicit Module(PyModuleDef* def) {
        static_assert(
            ref != Ref::BORROW,
            "Cannot construct a non-owning reference to a new module"
        );
        this->obj = PyModule_Create(def);
        if (this->obj == nullptr) {
            throw catch_python();
        }
    }

    /* Create a Python module from a PyModuleDef* struct with an optional required API
    version.  If the `api_version` argument does not match the version of the running
    interpreter, a RuntimeWarning will be emitted.  Users should prefer the standard
    PyModuleDef* constructor in almost all circumstances. */
    explicit Module(PyModuleDef* def, int api_version) {
        static_assert(
            ref != Ref::BORROW,
            "Cannot construct a non-owning reference to a new module"
        );
        this->obj = PyModule_Create2(def);
        if (this->obj == nullptr) {
            throw catch_python();
        }
    }

    /* Construct a Python module with the given name. */
    explicit Module(const char* name) {
        static_assert(
            ref != Ref::BORROW,
            "Cannot construct a non-owning reference to a new module"
        );
        this->obj = PyModule_New(name);
        if (this->obj == nullptr) {
            throw catch_python();
        }
    }

    /* Construct a Python module with the given name. */
    explicit Module(const std::string& name) : Module(name.c_str()) {}

    /* Construct a Python module with the given name. */
    explicit Module(const std::string_view& name) : Module(name.data()) {}

    /* Implicitly convert a python::Module into the PyModuleDef* from which it was
    created.  Can return null if the module wasn't created from a PyModuleDef*. */
    inline operator PyModuleDef*() const noexcept {
        return PyModule_GetDef(this->obj);
    }

    //////////////////////////////////
    ////    PyModule_* METHODS    ////
    //////////////////////////////////

    /* Get the module's filename. */
    inline String filename() const {
        PyObject* result = PyModule_GetFilenameObject(this->obj);
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    /* Get the module's name. */
    inline String name() const {
        PyObject* result = PyModule_GetNameObj(this->obj);
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    /* Get the module's namespace. */
    inline Dict dict() const {
        PyObject* result = PyModule_GetDict(this->obj);
        if (result == nullptr) {
            throw catch_python();
        }
        return {result};
    }

    /* Add an object to the module under the given name. */
    void add_object(const char* name, Object<Ref::BORROW> obj) {
        #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 10)
            if (PyModule_AddObjectRef(this->obj, name, obj) < 0) {
                throw catch_python();
            }

        #else
            obj.incref();
            if (PyModule_AddObject(this->obj, name, obj) < 0) {
                obj.decref();
                throw catch_python();
            }

        #endif
    }

    /* Add an object to the module under the given name. */
    void add_object(const std::string& name, Object<Ref::BORROW> obj) {
        add_object(name.c_str(), obj);
    }

    /* Add an object to the module under the given name. */
    void add_object(const std::string_view& name, Object<Ref::BORROW> obj) {
        add_object(name.data(), obj);
    }

    // TODO: PyModule_AddIntConstant
    // TODO: PyModule_AddStringConstant

    #if (Py_MAJOR_VERSION >= 3 && Py_MINOR_VERSION >= 9)

    /* Add a type to the module.  The name of the type is taken from the last component
    of `PyTypeObject.tp_name` after the final dot.  Note that this calls
    `PyType_Ready()` internally, so users can omit that function before calling this
    method in a module initialization function. */
    void add_type(PyTypeObject* type) {
        if (PyModule_AddType(this->obj, type) < 0) {
            throw catch_python();
        }
    }

    #endif

};




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


//////////////////////////////
////    GLOBAL OBJECTS    ////
//////////////////////////////


/* The built-in NotImplemented object. */
class NotImplementedType : public Object<Ref::BORROW, NotImplementedType> {
    NotImplementedType() : Object<Ref::BORROW>(Py_NotImplemented) {}
};


const NotImplementedType NotImplemented;




///////////////////////
////    NUMBERS    ////
///////////////////////


/* An extension of python::Object that represents a complex number in Python. */
template <Ref ref = Ref::STEAL>
class Complex : public Object<ref> {
    using Base = Object<ref>;

public:
    using Base::Base;
    using Base::operator=;

    /* Implicitly convert a PyObject* into a python::Complex. */
    Complex(PyObject* obj) : Base([&] {
        if (obj == nullptr || !PyComplex_Check(obj)) {
            throw TypeError("expected a complex number");
        }
        return obj;
    }()) {}

    /* Implicitly convert a C double into a python::Complex, with an optional imaginary
    component. */
    Complex(double real, double imag = 0) {
        static_assert(
            ref == Ref::BORROW,
            "Cannot construct a non-owning reference to a new complex number object"
        );
        this->obj = PyComplex_FromDoubles(real, imag);
        if (this->obj == nullptr) {
            throw catch_python();
        }
    }

    /* Implicitly convert a python::Complex into a PyComplexObject*. */
    inline operator PyComplexObject*() const noexcept {
        return reinterpret_cast<PyComplexObject*>(this->obj);
    }

    /* Implicitly convert a python::Complex into a C double representing the real
    component. */
    inline operator double() const {
        return real();
    }

    /* Get the real component of the complex number as a C double. */
    inline double real() const {
        return PyComplex_RealAsDouble(this->obj);
    }

    /* Get the imaginary component of the complex number as a C double. */
    inline double imag() const {
        return PyComplex_ImagAsDouble(this->obj);
    }

};


// TODO: datetime/timedelta, with implicit conversions to std::chrono




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


/* An extension of python::Object that represents a Python slice. */
template <Ref ref = Ref::STEAL>
class Slice : public Object<ref, Slice> {
    using Base = Object<ref>;

    PyObject* _start;
    PyObject* _stop;
    PyObject* _step;

public:
    using Base::Base;
    using Base::operator=;

    /* Default constructor.  Initializes to an empty slice (all Nones). */
    Slice() :
        _start(Py_NewRef(Py_None)), _stop(Py_NewRef(Py_None)),
        _step(Py_NewRef(Py_None))
    {
        static_assert(
            ref != Ref::BORROW,
            "Cannot construct a non-owning reference to a new slice object"
        );
        this->obj = PySlice_New(nullptr, nullptr, nullptr);
    }

    /* Implicitly convert a PyObject* into a python::Slice. */
    Slice(PyObject* obj) : Base([&] {
        if (obj == nullptr || !PySlice_Check(obj)) {
            throw TypeError("expected a slice");
        }
        return obj;
    }()) {
        // cache start, stop, step attributes to avoid repeated lookups
        _start = this->getattr("start").unwrap();
        _stop = this->getattr("stop").unwrap();
        _step = this->getattr("step").unwrap();
    }

    /* Copy constructor. */
    Slice(const Slice& other) :
        Base(other), _start(other._start), _stop(other._stop), _step(other._step)
    {
        if constexpr (ref != Ref::BORROW) {
            Py_XINCREF(other._start);
            Py_XINCREF(other._stop);
            Py_XINCREF(other._step);
        }
    }

    /* Move constructor. */
    Slice(Slice&& other) :
        Base(std::move(other)), _start(other._start), _stop(other._stop),
        _step(other._step)
     {
        other._start = nullptr;
        other._stop = nullptr;
        other._step = nullptr;
    }

    /* Copy assignment. */
    Slice& operator=(const Slice& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        if constexpr (ref != Ref::BORROW) {
            Py_XDECREF(_start);
            Py_XDECREF(_stop);
            Py_XDECREF(_step);
        }
        _start = other._start;
        _stop = other._stop;
        _step = other._step;
        if constexpr (ref != Ref::BORROW) {
            Py_XINCREF(_start);
            Py_XINCREF(_stop);
            Py_XINCREF(_step);
        }
        return *this;
    }

    /* Move assignment. */
    Slice& operator=(Slice&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(std::move(other));
        if constexpr (ref != Ref::BORROW) {
            Py_XDECREF(_start);
            Py_XDECREF(_stop);
            Py_XDECREF(_step);
        }
        _start = other._start;
        _stop = other._stop;
        _step = other._step;
        other._start = nullptr;
        other._stop = nullptr;
        other._step = nullptr;
        return *this;
    }

    /* Release the Python slice on destruction. */
    ~Slice() {
        if constexpr (ref != Ref::BORROW) {
            Py_XDECREF(_start);
            Py_XDECREF(_stop);
            Py_XDECREF(_step);
        }
    }

    /////////////////////////////////
    ////    PySlice_* METHODS    ////
    /////////////////////////////////

    /* Get the start index of the slice. */
    inline Object<Ref::BORROW> start() const {
        return Object<Ref::BORROW>(_start);
    }

    /* Get the stop index of the slice. */
    inline Object<Ref::BORROW> stop() const {
        return Object<Ref::BORROW>(_stop);
    }

    /* Get the step index of the slice. */
    inline Object<Ref::BORROW> step() const {
        return Object<Ref::BORROW>(_step);
    }

    /* Normalize the slice for a given sequence length, returning a 4-tuple containing
    the start, stop, step, and number of elements included in the slice. */
    inline auto normalize(Py_ssize_t length) const
        -> std::tuple<Py_ssize_t, Py_ssize_t, Py_ssize_t, size_t>
    {
        Py_ssize_t nstart, nstop, nstep, nlength;
        if (PySlice_GetIndicesEx(this->obj, length, &nstart, &nstop, &nstep, &nlength)) {
            throw catch_python();
        }
        return std::make_tuple(nstart, nstop, nstep, nlength);
    }

};


// TODO: MemoryView, Iterators, Generators, Context managers, weak references, capsules, etc.

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
