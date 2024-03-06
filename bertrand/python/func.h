#ifndef BERTRAND_PYTHON_INCLUDED
#error "This file should not be included directly.  Please include <bertrand/python.h> instead."
#endif

#ifndef BERTRAND_PYTHON_FUNC_H
#define BERTRAND_PYTHON_FUNC_H

#include "common.h"
#include "dict.h"
#include "str.h"
#include "tuple.h"
#include "type.h"


// TODO: move callable() proxy up to start of file and have Function use it for its
// like<> template.  Can then remove it from common.h
// -> or place the zero-arg callable<> check in common.h and hook into it here.  That
// would allow us to define it in python.h and decouple it here.


namespace bertrand {
namespace py {


namespace impl {

    static const Static<Type> PyProperty = reinterpret_borrow<Type>(
        reinterpret_cast<PyObject*>(&PyProperty_Type)
    );

}


/* A new subclass of pybind11::object that represents a compiled Python code object,
enabling seamless embedding of Python as a scripting language within C++.

This class is extremely powerful, and is best explained by example:

    static const py::Static<py::Code> script(R"(
        import numpy as np
        print(np.arange(10))
    )");

    script();  // prints [0 1 2 3 4 5 6 7 8 9]

.. note::

    Note that the script in this example is stored with static duration, which means
    that it will only be compiled once and then cached for the duration of the program.
    Bertrand will automatically free it when the program exits, without interfering
    with the Python interpreter.

This creates an embedded Python script that can be executed as a normal function.
Here, the script is stateless, and can be executed without context.  Most of the time,
this won't be the case, and data will need to be passed into the script to populate its
namespace.  For instance:

    static const py::Static<py::Code> script = R"(
        print("Hello, " + name + "!")  # name is not defined in this context
    )"_python;

.. note::

    Note the user-defined `_python` literal used to create the script.  This is
    equivalent to calling the `Code` constructor, but is more convenient and readable.

If we try to execute this script without a context, we'll get a ``NameError`` just
like normal Python:

    script();  // NameError: name 'name' is not defined

We can solve this by building a context dictionary and passing it into the script as
its global namespace.

    script({{"name", "world"}});  // prints Hello, world!

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

Combining these features allows us to create a two-way data pipeline marrying C++ and
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

    static const py::Static<py::Code> script1 = R"(
        x = 1
        y = 2
    )"_python;

    static const py::Static<py::Code> script2 = R"(
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

    static const py::Static<py::Code> script = R"(
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

    template <typename T>
    static PyObject* compile(const T& text) {
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
            "<embedded C++ script>",
            Py_file_input
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::is_same_or_subclass_of<Code, T>; }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_OBJECT_COMMON(Base, Code, PyCode_Check);

    /* Default constructor deleted to throw compile errors when a script is declared
    without an implementation. */
    Code() = delete;

    /* Parse and compile a source string into a Python code object. */
    explicit Code(const char* source) : Base(compile(source), stolen_t{}) {}

    /* Parse and compile a source string into a Python code object. */
    explicit Code(const std::string& source) : Base(compile(source), stolen_t{}) {}

    /* Parse and compile a source string into a Python code object. */
    explicit Code(const std::string_view& source) : Code(source.data()) {}

    ////////////////////////////////
    ////    PyCode_* METHODS    ////
    ////////////////////////////////

    /* Execute the code object without context. */
    inline Dict operator()() const {
        Dict context;
        PyObject* result = PyEval_EvalCode(this->ptr(), context.ptr(), context.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        Py_DECREF(result);  // always None
        return context;
    }

    /* Execute the code object with the given context. */
    inline Dict& operator()(Dict& context) const {
        PyObject* result = PyEval_EvalCode(this->ptr(), context.ptr(), context.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        Py_DECREF(result);  // always None
        return context;
    }

    /* Execute the code object with the given context. */
    inline Dict operator()(Dict&& context) const {
        return std::move((*this)(context));
    }

    /////////////////////
    ////    SLOTS    ////
    /////////////////////

    /* A proxy for inspecting the code object's PyCodeObject* slots. */
    struct Slots {
        PyCodeObject* code_obj;

        /* Get the name of the file from which the code was compiled. */
        inline Str filename() const {
            return reinterpret_borrow<Str>(code_obj->co_filename);
        }

        /* Get the function's base name. */
        inline Str name() const {
            return reinterpret_borrow<Str>(code_obj->co_name);
        }

        /* Get the function's qualified name. */
        inline Str qualname() const {
            return reinterpret_borrow<Str>(code_obj->co_qualname);
        }

        /* Get the first line number of the function. */
        inline Py_ssize_t line_number() const noexcept {
            return code_obj->co_firstlineno;
        }

        /* Get the total number of positional arguments for the function, including
        positional-only arguments and those with default values (but not variable
        or keyword-only arguments). */
        inline Py_ssize_t n_args() const noexcept {
            return code_obj->co_argcount;
        }

        /* Get the number of positional-only arguments for the function, including
        those with default values.  Does not include variable positional or keyword
        arguments. */
        inline Py_ssize_t n_positional() const noexcept {
            return code_obj->co_posonlyargcount;
        }

        /* Get the number of keyword-only arguments for the function, including those
        with default values.  Does not include positional-only or variable
        positional/keyword arguments. */
        inline Py_ssize_t n_keyword() const noexcept {
            return code_obj->co_kwonlyargcount;
        }

        /* Get the number of local variables used by the function (including all
        parameters). */
        inline Py_ssize_t n_locals() const noexcept {
            return code_obj->co_nlocals;
        }

        // /* Get a tuple containing the names of the local variables in the function,
        // starting with parameter names. */
        // inline Tuple locals() const {
        //     return reinterpret_borrow<Tuple>(code_obj->co_varnames);
        // }

        // /* Get a tuple containing the names of local variables that are referenced by
        // nested functions within this function (i.e. those that are stored in a
        // PyCell). */
        // inline Tuple cellvars() const {
        //     return reinterpret_borrow<Tuple>(code_obj->co_cellvars);
        // }

        // /* Get a tuple containing the names of free variables in the function (i.e.
        // those that are not stored in a PyCell). */
        // inline Tuple freevars() const {
        //     return reinterpret_borrow<Tuple>(code_obj->co_freevars);
        // }

        /* Get the required stack space for the code object. */
        inline Py_ssize_t stack_size() const noexcept {
            return code_obj->co_stacksize;
        }

        // /* Get the bytecode buffer representing the sequence of instructions in the
        // function. */
        // inline const char* bytecode() const {
        //     return code_obj->co_code;
        // }

        /* Get a tuple containing the literals used by the bytecode in the function. */
        inline Tuple constants() const {
            return reinterpret_borrow<Tuple>(code_obj->co_consts);
        }

        /* Get a tuple containing the names used by the bytecode in the function. */
        inline Tuple names() const {
            return reinterpret_borrow<Tuple>(code_obj->co_names);
        }

        /* Get an integer encoding flags for the Python interpreter. */
        inline int flags() const noexcept {
            return code_obj->co_flags;
        }

    };

    /* Get the code object's internal Python slots. */
    inline Slots slots() const {
        return {reinterpret_cast<PyCodeObject*>(this->ptr())};
    }

};


/* A new subclass of pybind11::object that represents a Python interpreter frame, which
can be used to introspect its current state. */
class Frame : public Object {
    using Base = Object;

    inline PyFrameObject* self() const {
        return reinterpret_cast<PyFrameObject*>(this->ptr());
    }

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::is_same_or_subclass_of<Frame, T>; }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_OBJECT_COMMON(Base, Frame, PyFrame_Check);

    /* Default constructor.  Initializes to the current execution frame. */
    Frame() : Base(reinterpret_cast<PyObject*>(PyEval_GetFrame()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw RuntimeError("no frame is currently executing");
        }
    }

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

    /////////////////////////////////
    ////    PyFrame_* METHODS    ////
    /////////////////////////////////

    /* Get the next outer frame from this one. */
    inline Frame back() const {
        PyFrameObject* result = PyFrame_GetBack(self());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Frame>(reinterpret_cast<PyObject*>(result));
    }

    /* Get the code object associated with this frame. */
    inline Code code() const {
        // PyFrame_GetCode() is never null
        return reinterpret_steal<Code>(
            reinterpret_cast<PyObject*>(PyFrame_GetCode(self()))
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
            throw error_already_set();
        }
        return reinterpret_steal<Object>(result);
    }

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 11)

        /* Get the frame's builtin namespace. */
        inline Dict builtins() const {
            return reinterpret_steal<Dict>(PyFrame_GetBuiltins(self()));
        }

        /* Get the frame's globals namespace. */
        inline Dict globals() const {
            PyObject* result = PyFrame_GetGlobals(self());
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Dict>(result);
        }

        /* Get the frame's locals namespace. */
        inline Dict locals() const {
            PyObject* result = PyFrame_GetLocals(self());
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Dict>(result);
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
        the bytecode of the last instruction executed by the frame's code object. */
        inline int last_instruction() const noexcept {
            return PyFrame_GetLasti(self());
        }

    #endif

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12)

        /* Get a named variable from the frame's context.  Can raise if the variable is
        not present in the frame. */
        inline Object get(PyObject* name) const {
            PyObject* result = PyFrame_GetVar(self(), name);
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Object>(result);
        }

        /* Get a named variable from the frame's context.  Can raise if the variable is
        not present in the frame. */
        inline Object get(const char* name) const {
            PyObject* result = PyFrame_GetVarString(self(), name);
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Object>(result);
        }

        /* Get a named variable from the frame's context.  Can raise if the variable is
        not present in the frame. */
        inline Object get(const std::string& name) const {
            return get(name.c_str());
        }

        /* Get a named variable from the frame's context.  Can raise if the variable is
        not present in the frame. */
        inline Object get(const std::string_view& name) const {
            return get(name.data());
        }

    #endif

};


/* Wrapper around a pybind11::Function that allows it to be constructed from a C++
lambda or function pointer, and enables extra introspection via the C API. */
class Function : public Object {
    using Base = Object;

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::is_callable_any<T>; }

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    BERTRAND_OBJECT_COMMON(Base, Function, PyFunction_Check);

    /* Default constructor deleted to avoid confusion + possibility of nulls. */
    Function() = delete;

    /* Implicitly convert a C++ function or callable object into a py::Function. */
    template <
        typename T,
        std::enable_if_t<
            check<std::decay_t<T>>() && !impl::is_python<std::decay_t<T>>,
        int> = 0
    >
    Function(T&& func) :
        Base(pybind11::cpp_function(std::forward<T>(func)).release(), stolen_t{})
    {}

    ///////////////////////////////
    ////    PyFunction_ API    ////
    ///////////////////////////////

    template <typename... Args>
    inline Object operator()(Args&&... args) const {
        return Base::operator()(std::forward<Args>(args)...);
    }

    /* Get the module in which this function was defined. */
    inline Module module_() const {
        PyObject* result = PyFunction_GetModule(this->ptr());
        if (result == nullptr) {
            throw TypeError("function has no module");
        }
        return reinterpret_borrow<Module>(result);
    }

    /* Get the code object that is executed when this function is called. */
    inline Code code() const {
        PyObject* result = PyFunction_GetCode(this->ptr());
        if (result == nullptr) {
            throw RuntimeError("function does not have a code object");
        }
        // PyFunction_GetCode returns a borrowed reference
        return reinterpret_borrow<Code>(result);
    }

    /* Get the name of the file from which the code was compiled. */
    inline std::string filename() const {
        return code().slots().filename();
    }

    /* Get the first line number of the function. */
    inline size_t line_number() const {
        return code().slots().line_number();
    }

    /* Get the function's base name. */
    inline std::string name() const {
        return code().slots().name();
    }

    /* Get the function's qualified name. */
    inline std::string qualname() const {
        return code().slots().qualname();
    }

    /* Get the closure associated with the function.  This is a Tuple of cell objects
    containing data captured by the function. */
    inline Tuple closure() const {
        PyObject* result = PyFunction_GetClosure(this->ptr());
        if (result == nullptr) {
            return {};
        }
        return reinterpret_borrow<Tuple>(result);
    }

    /* Set the closure associated with the function.  The input must be a Tuple or
    nullopt. */
    inline void closure(std::optional<Tuple> closure) {
        PyObject* item = closure ? closure.value().ptr() : Py_None;
        if (PyFunction_SetClosure(this->ptr(), item)) {
            throw error_already_set();
        }
    }

    /* Get the type annotations for the function.  This is returned as a mutable
    dictionary that can be written to in order to change the annotations. */
    inline Dict annotations() const {
        PyObject* result = PyFunction_GetAnnotations(this->ptr());
        if (result == nullptr) {
            return {};
        }
        return reinterpret_borrow<Dict>(result);
    }

    /* Set the type annotations for the function.  The input must be a Dict or
    nullopt. */
    inline void annotations(std::optional<Dict> annotations) {
        PyObject* item = annotations ? annotations.value().ptr() : Py_None;
        if (PyFunction_SetAnnotations(this->ptr(), item)) {
            throw error_already_set();
        }
    }

    /* Get the default values for the function.  This is a Tuple of values for the
    function's parameters. */
    inline Tuple defaults() const {
        PyObject* result = PyFunction_GetDefaults(this->ptr());
        if (result == nullptr) {
            return {};
        }
        return reinterpret_borrow<Tuple>(result);
    }

    /* Set the default values for the function.  The input must be a Tuple or
    nullopt. */
    inline void defaults(std::optional<Tuple> defaults) {
        PyObject* item = defaults ? defaults.value().ptr() : Py_None;
        if (PyFunction_SetDefaults(this->ptr(), item)) {
            throw error_already_set();
        }
    }

    /* Get the globals dictionary associated with the function object. */
    inline Dict globals() const {
        PyObject* result = PyFunction_GetGlobals(this->ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_borrow<Dict>(result);
    }

    /* Get the total number of positional or keyword arguments for the function,
    including positional-only parameters and excluding keyword-only parameters. */
    inline size_t n_args() const {
        return code().slots().n_args();
    }

    /* Get the number of positional-only arguments for the function.  Does not include
    variable positional or keyword arguments. */
    inline size_t n_positional() const {
        return code().slots().n_positional();
    }

    /* Get the number of keyword-only arguments for the function.  Does not include
    positional-only or variable positional/keyword arguments. */
    inline size_t n_keyword() const {
        return code().slots().n_keyword();
    }

};


/* New subclass of pybind11::object that represents a bound method at the Python
level. */
class Method : public Object {

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::is_same_or_subclass_of<Method, T>; }


    BERTRAND_OBJECT_COMMON(Object, Method, PyInstanceMethod_Check)

    /* Default constructor deleted to avoid confusion + possibility of nulls. */
    Method() = delete;

    /* Wrap an existing Python function as a method descriptor. */
    Method(const Function& func) : Object(PyInstanceMethod_New(func.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

};


/* New subclass of pybind11::object that represents a bound classmethod at the Python
level. */
class ClassMethod : public Object {

    inline static bool check_classmethod(PyObject* obj) {
        int result = PyObject_IsInstance(
            obj,
            reinterpret_cast<PyObject*>(&PyClassMethodDescr_Type)
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::is_same_or_subclass_of<ClassMethod, T>; }

    BERTRAND_OBJECT_COMMON(Object, ClassMethod, check_classmethod)

    /* Default constructor deleted to avoid confusion + possibility of nulls. */
    ClassMethod() = delete;

    /* Wrap an existing Python function as a classmethod descriptor. */
    ClassMethod(Function func) : Object(PyClassMethod_New(func.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

};


/* Wrapper around a pybind11::StaticMethod that allows it to be constructed from a
C++ lambda or function pointer, and enables extra introspection via the C API. */
class StaticMethod : public Object {

    static bool check_staticmethod(PyObject* obj) {
        int result = PyObject_IsInstance(
            obj,
            reinterpret_cast<PyObject*>(&PyStaticMethod_Type)
        );
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::is_same_or_subclass_of<StaticMethod, T>; }

    BERTRAND_OBJECT_COMMON(Object, StaticMethod, check_staticmethod);

    /* Default constructor deleted to avoid confusion + possibility of nulls. */
    StaticMethod() = delete;

    /* Wrap an existing Python function as a staticmethod descriptor. */
    StaticMethod(Function func) : Object(PyStaticMethod_New(func.ptr()), stolen_t{}) {
        if (m_ptr == nullptr) {
            throw error_already_set();
        }
    }

};


/* New subclass of pybind11::object that represents a property descriptor at the
Python level. */
class Property : public Object {

    inline static bool check_property(PyObject* obj) {
        int result = PyObject_IsInstance(obj, impl::PyProperty->ptr());
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

public:
    static Type type;

    template <typename T>
    static constexpr bool check() { return impl::is_same_or_subclass_of<Property, T>; }

    BERTRAND_OBJECT_COMMON(Object, Property, check_property);

    /* Default constructor deleted to avoid confusion + possibility of nulls. */
    Property() = delete;

    /* Wrap an existing Python function as a getter in a property descriptor. */
    Property(Function getter) : Object(impl::PyProperty(getter).release(), stolen_t{}) {}

    /* Wrap existing Python functions as getter and setter in a property descriptor. */
    Property(Function getter, Function setter) :
        Object(impl::PyProperty(getter, setter).release(), stolen_t{})
    {}

    /* Wrap existing Python functions as getter, setter, and deleter in a property
    descriptor. */
    Property(Function getter, Function setter, Function deleter) :
        Object(impl::PyProperty(getter, setter, deleter).release(), stolen_t{})
    {}

};


}  // namespace python
}  // namespace bertrand


/* User-defined literal to make embedding python scripts as easy as possible */
inline bertrand::py::Code operator"" _python(const char* source, size_t size) {
    return bertrand::py::Code(std::string_view(source, size));
}


#endif  // BERTRAND_PYTHON_FUNC_H
