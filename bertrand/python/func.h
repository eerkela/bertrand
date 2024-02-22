#ifndef BERTRAND_PYTHON_FUNC_H
#define BERTRAND_PYTHON_FUNC_H

#include <limits>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_set>

#include "common.h"
#include "dict.h"
#include "str.h"
#include "tuple.h"


namespace bertrand {
namespace py {


namespace impl {

    /* NOTE: storing code objects as static variables is dangerous since the Python
    interpreter may be already destroyed by the time the code object's destructor is
    called, which causes a segfault.  There is a workaround, but it requires us to
    maintain a global set of living code objects and clean them up manually just before
    Python exits. */
    std::unordered_set<Code*> LIVING_CODE_OBJECTS;
    static void clean_up_living_code_objects();

}


/* A new subclass of pybind11::object that represents a compiled Python code object,
enabling seamless embedding of Python as a scripting language within C++.

This class is extremely powerful, and is best explained by example:

    static py::Code script(R"(
        import numpy as np
        print(np.arange(10))
    )");

    script();  // prints [0 1 2 3 4 5 6 7 8 9]

This creates an embedded Python script that can be executed as a function.  Here, the
script is stateless, and can be executed without context.  Most of the time, this won't
be the case, and data will need to be passed into the script to populate its namespace.
For instance:

    static py::Code script = R"(
        print("Hello, " + name + "!")  # name is not defined in this context
    )"_python;

Note the user-defined `_python` literal used to create the script.  This is equivalent
to calling the `Code` constructor, but is more convenient and readable.  If we try to
execute this script without a context, we'll get an error:

    script();  // raises NameError: name 'name' is not defined

We can solve this by building a dictionary and passing it into the script:

    script({{"name", "world"}});  // prints Hello, world!

This uses any of the ordinary py::Dict constructor, which can take arbitrary C++ values
as long as there is a corresponding pybind11 type, making it possible to seamlessly pass
data from C++ to Python.

If we want to do the opposite and extract data from Python back to C++, we can use the
return value of the script, which is a dictionary containing the script's namespace
after it has been executed.  For instance:

    py::Dict context = R"(
        x = 1
        y = 2
        z = 3
    )"_python();

    py::print(context);  // prints {"x": 1, "y": 2, "z": 3}

We can combine these features to create a two-way data pipeline between C++ and Python:

    py::Int z = R"(
        def func(x, y):
            return x + y

        z = func(a, b)
    )"_python({{"a", 1}, {"b", 2}})["z"];

    py::print(z);  // prints 3

Which seamlessly marries the two.  In this example, data originates in C++, passes
through python for processing, and then returns smoothly to C++ with automatic error
handling, reference counting, and type-safe conversions at each step.

In the previous example, the input dictionary exists only for the duration of the
script's execution, and is discarded immediately afterwards.  However, it is also
possible to pass a mutable reference to an external dictionary, which will be updated
in-place during the script's execution.  This allows multiple scripts to be chained
using a shared context, without ever leaving the Python interpreter.  For instance:

    static py::Code script1 = R"(
        x = 1
        y = 2
    )"_python;

    static py::Code script2 = R"(
        z = x + y
        del x, y
    )"_python;

    py::Dict context;
    script1(context);
    script2(context);
    py::print(context);  // prints {"z": 3}

Users can, of course, inspect or modify the context between scripts, either to extract
results or pass new data into the next script.  This makes it possible to create
complex, mixed-language workflows that are fully integrated and seamless in both
directions.

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
    )"_python(context)["result"].cast<std::vector<int>>();

This is a powerful feature that allows Python to be used as an inline scripting
language in any C++ application, with full native compatibility in both directions.
The script is evaluated just like an ordinary Python file, and there are no
restrictions on what can be done inside it.  This includes importing modules, defining
classes and functions to be exported back to C++, interacting with the file system,
third-party libraries, client code, etc.  Similarly, it is executed just like normal
Python, and should not suffer any significant performance penalties beyond copying data
into or out of the context.  This is especially true for static code objects, which are
compiled once and then cached for repeated use.

    static py::Code script = R"(
        print(x)
    )"_python;

    script({{"x", "hello"}});
    script({{"x", "from"}});
    script({{"x", "the"}});
    script({{"x", "other"}});
    script({{"x", "side"}});
*/
class Code : public impl::EqualCompare<Code> {
    using Base = pybind11::handle;
    using Compare = impl::EqualCompare<Code>;

    /* NOTE: we can't directly inherit from pybind11::object because that causes memory
    access errors when code objects are stored as static variables.  Since that is the
    intended use case, we provide a workaround using Python's `atexit` module. */
    friend void impl::clean_up_living_code_objects();
    friend class Frame;
    friend class Function;
    PyObject* m_ptr;

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

    explicit Code(PyObject* ptr) : m_ptr(ptr) {
        impl::LIVING_CODE_OBJECTS.insert(this);
    }

public:
    static py::Type Type;

    /* Parse and compile a source string into a Python code object. */
    explicit Code(const char* source) : m_ptr(compile(source)) {
        impl::LIVING_CODE_OBJECTS.insert(this);
    }

    /* Parse and compile a source string into a Python code object. */
    explicit Code(const std::string& source) : m_ptr(compile(source)) {
        impl::LIVING_CODE_OBJECTS.insert(this);
    }

    /* Parse and compile a source string into a Python code object. */
    explicit Code(const std::string_view& source) : Code(source.data()) {}

    /* Copy constructor. */
    Code(const Code& other) : m_ptr(other.m_ptr) {
        Py_XINCREF(m_ptr);
        impl::LIVING_CODE_OBJECTS.insert(this);
    }

    /* Move constructor. */
    Code(Code&& other) : m_ptr(other.m_ptr) {
        other.m_ptr = nullptr;
        impl::LIVING_CODE_OBJECTS.erase(&other);
        impl::LIVING_CODE_OBJECTS.insert(this);
    }

    /* Copy assignment operator. */
    Code& operator=(const Code& other) {
        if (this != &other) {
            PyObject* temp = m_ptr;
            Py_XINCREF(m_ptr);
            m_ptr = other.m_ptr;
            Py_XDECREF(temp);
        }
        return *this;
    }

    /* Move assignment operator. */
    Code& operator=(Code&& other) {
        if (this != &other) {
            PyObject* temp = m_ptr;
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
            Py_XDECREF(temp);
        }
        return *this;
    }

    /* Destructor.  Note that we can't use Py_XDECREF() here because it causes memory
    access errors when code objects are stored as static variables.  */
    ~Code() {
        impl::LIVING_CODE_OBJECTS.erase(this);
        if (m_ptr != nullptr) {
            Py_DECREF(m_ptr);
            m_ptr = nullptr;
        }
    }

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
        return (*this)(context);
    }

    inline PyObject* ptr() const {
        return m_ptr;
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

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Compare::operator==;
    using Compare::operator!=;

    inline operator bool() const noexcept {
        return m_ptr != nullptr;
    }

    inline friend std::ostream& operator<<(std::ostream& os, const Code& code) {
        PyObject* result = PyObject_Repr(code.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        os << py::cast(result);
        return os;
    }

};


/* A new subclass of pybind11::object that represents a Python interpreter frame, which
can be used to introspect its current state. */
class Frame :
    public pybind11::object,
    public impl::EqualCompare<Frame>
{
    using Base = pybind11::object;
    using Compare = impl::EqualCompare<Frame>;

    inline PyFrameObject* as_frame() const {
        return reinterpret_cast<PyFrameObject*>(this->ptr());
    }

    static PyObject* convert_to_frame(PyObject* obj) {
        throw TypeError("cannot convert to py::Frame");
    }

public:
    static py::Type Type;

    CONSTRUCTORS(Frame, PyFrame_Check, convert_to_frame);

    /* Default constructor.  Initializes to the current execution frame. */
    inline Frame() : Base([] {
        PyFrameObject* result = PyEval_GetFrame();
        if (result == nullptr) {
            throw RuntimeError("no frame is currently executing");
        }
        return reinterpret_cast<PyObject*>(result);
    }(), stolen_t{}) {}

    /* Skip backward a number of frames on construction. */
    explicit Frame(size_t skip) : Base([&skip] {
        PyFrameObject* result = PyEval_GetFrame();
        if (result == nullptr) {
            throw RuntimeError("no frame is currently executing");
        }
        for (size_t i = 0; i < skip; ++i) {
            result = PyFrame_GetBack(result);
            if (result == nullptr) {
                throw IndexError("frame index out of range");
            }
        }
        return reinterpret_cast<PyObject*>(result);
    }(), stolen_t{}) {}

    /////////////////////////////////
    ////    PyFrame_* METHODS    ////
    /////////////////////////////////

    /* Get the next outer frame from this one. */
    inline Frame back() const {
        PyFrameObject* result = PyFrame_GetBack(as_frame());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Frame>(reinterpret_cast<PyObject*>(result));
    }

    /* Get the code object associated with this frame. */
    inline Code code() const {
        // PyFrame_GetCode() is never null
        return Code(reinterpret_cast<PyObject*>(PyFrame_GetCode(as_frame())));
    }

    /* Get the line number that the frame is currently executing. */
    inline int line_number() const noexcept {
        return PyFrame_GetLineNumber(as_frame());
    }

    /* Execute the code object stored within the frame using its current context.  This
    is the main entry point for the Python interpreter, and is used behind the scenes
    whenever a program is run. */
    inline Object operator()() const {
        PyObject* result = PyEval_EvalFrame(as_frame());
        if (result == nullptr) {
            throw error_already_set();
        }
        return reinterpret_steal<Object>(result);
    }

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 11)

        /* Get the frame's builtin namespace. */
        inline Dict builtins() const {
            return reinterpret_steal<Dict>(PyFrame_GetBuiltins(as_frame()));
        }

        /* Get the frame's globals namespace. */
        inline Dict globals() const {
            PyObject* result = PyFrame_GetGlobals(as_frame());
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Dict>(result);
        }

        /* Get the frame's locals namespace. */
        inline Dict locals() const {
            PyObject* result = PyFrame_GetLocals(as_frame());
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Dict>(result);
        }

        /* Get the generator, coroutine, or async generator that owns this frame, or
        nullopt if this frame is not owned by a generator. */
        inline std::optional<Object> generator() const {
            PyObject* result = PyFrame_GetGenerator(as_frame());
            if (result == nullptr) {
                return std::nullopt;
            } else {
                return std::make_optional(reinterpret_steal<Object>(result));
            }
        }

        /* Get the "precise instruction" of the frame object, which is an index into
        the bytecode of the last instruction executed by the frame's code object. */
        inline int last_instruction() const noexcept {
            return PyFrame_GetLasti(as_frame());
        }

    #endif

    #if (Py_MAJOR_VERSION >= 3 && PY_MINOR_VERSION >= 12)

        /* Get a named variable from the frame's context.  Can raise if the variable is
        not present in the frame. */
        inline Object get(PyObject* name) const {
            PyObject* result = PyFrame_GetVar(as_frame(), name);
            if (result == nullptr) {
                throw error_already_set();
            }
            return reinterpret_steal<Object>(result);
        }

        /* Get a named variable from the frame's context.  Can raise if the variable is
        not present in the frame. */
        inline Object get(const char* name) const {
            PyObject* result = PyFrame_GetVarString(as_frame(), name);
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

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Compare::operator==;
    using Compare::operator!=;
};


/* Wrapper around a pybind11::Function that allows it to be constructed from a C++
lambda or function pointer, and enables extra introspection via the C API. */
class Function :
    public pybind11::function,
    public impl::EqualCompare<Function>
{
    using Base = pybind11::function;
    using Compare = impl::EqualCompare<Function>;

    static PyObject* convert_to_function(PyObject* obj) {
        throw TypeError("cannot convert to function object");
    }

public:
    static py::Type Type;

    CONSTRUCTORS(Function, PyFunction_Check, convert_to_function);

    template <
        typename Func,
        std::enable_if_t<!std::is_base_of_v<pybind11::handle, std::decay_t<Func>>, int> = 0
    >
    Function(Func&& func) : Base([&func] {
        return pybind11::cpp_function(std::forward<Func>(func)).release();
    }(), stolen_t{}) {}

    ///////////////////////////////
    ////    PyFunction_ API    ////
    ///////////////////////////////

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
        return Code(result);
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
        if (PyFunction_SetClosure(this->ptr(), closure.value_or(None).ptr())) {
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
        if (PyFunction_SetAnnotations(this->ptr(), annotations.value_or(None).ptr())) {
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
        if (PyFunction_SetDefaults(this->ptr(), defaults.value_or(None).ptr())) {
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

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    using Compare::operator==;
    using Compare::operator!=;
};


/* New subclass of pybind11::object that represents a bound method at the Python
level. */
class Method :
    public pybind11::object,
    public impl::EqualCompare<Method>
{
    using Base = pybind11::object;
    using Compare = impl::EqualCompare<Method>;

    inline static PyObject* convert_to_method(PyObject* obj) {
        throw TypeError("cannot convert to py::Method");
    }

public:
    static py::Type Type;

    CONSTRUCTORS(Method, PyInstanceMethod_Check, convert_to_method);

    /* Wrap an existing Python function as a method descriptor. */
    Method(Function func) : Base([&func] {
        PyObject* result = PyInstanceMethod_New(func.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    using Compare::operator==;
    using Compare::operator!=;
};


/* New subclass of pybind11::object that represents a bound classmethod at the Python
level. */
class ClassMethod :
    public pybind11::object,
    public impl::EqualCompare<ClassMethod>
{
    using Base = pybind11::object;
    using Compare = impl::EqualCompare<ClassMethod>;

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

    inline static PyObject* convert_to_classmethod(PyObject* obj) {
        throw TypeError("cannot convert to py::ClassMethod");
    }

public:
    static py::Type Type;

    CONSTRUCTORS(ClassMethod, check_classmethod, convert_to_classmethod);

    /* Wrap an existing Python function as a classmethod descriptor. */
    ClassMethod(Function func) : Base([&func] {
        PyObject* result = PyClassMethod_New(func.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    using Compare::operator==;
    using Compare::operator!=;
};


/* Wrapper around a pybind11::StaticMethod that allows it to be constructed from a
C++ lambda or function pointer, and enables extra introspection via the C API. */
class StaticMethod :
    public pybind11::staticmethod,
    public impl::EqualCompare<StaticMethod>
{
    using Base = pybind11::staticmethod;
    using Compare = impl::EqualCompare<StaticMethod>;

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

    static PyObject* convert_to_staticmethod(PyObject* obj) {
        throw TypeError("cannot convert to staticmethod object");
    }

public:
    static py::Type Type;

    CONSTRUCTORS(StaticMethod, check_staticmethod, convert_to_staticmethod);

    /* Wrap an existing Python function as a staticmethod descriptor. */
    StaticMethod(Function func) : Base([&func] {
        PyObject* result = PyStaticMethod_New(func.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    using Compare::operator==;
    using Compare::operator!=;
};


namespace impl {

    static const Handle PyProperty = []() -> Handle {
        return reinterpret_cast<PyObject*>(&PyProperty_Type);
    }();

}


/* New subclass of pybind11::object that represents a property descriptor at the
Python level. */
class Property :
    public pybind11::object,
    public impl::EqualCompare<Property>
{
    using Base = pybind11::object;
    using Compare = impl::EqualCompare<Property>;

    inline static bool check_property(PyObject* obj) {
        int result = PyObject_IsInstance(obj, impl::PyProperty.ptr());
        if (result == -1) {
            throw error_already_set();
        }
        return result;
    }

    inline static PyObject* convert_to_property(PyObject* obj) {
        throw TypeError("cannot convert to py::Property");
    }

public:
    static py::Type Type;

    CONSTRUCTORS(Property, check_property, convert_to_property);

    /* Wrap an existing Python function as a getter in a property descriptor. */
    Property(Function getter) : Base([&getter] {
        return impl::PyProperty(getter).release();
    }(), stolen_t{}) {}

    /* Wrap existing Python functions as getter and setter in a property descriptor. */
    Property(Function getter, Function setter) : Base([&getter, &setter] {
        return impl::PyProperty(getter, setter).release();
    }(), stolen_t{}) {}

    /* Wrap existing Python functions as getter, setter, and deleter in a property
    descriptor. */
    Property(Function getter, Function setter, Function deleter) : Base([&] {
        return impl::PyProperty(getter, setter, deleter).release();
    }(), stolen_t{}) {}

    using Compare::operator==;
    using Compare::operator!=;
};


namespace impl {

    /* A tag struct containing SFINAE information information about a C++ or python
    function pointer that was passed to `py::callable()`.  The tag is implicitly
    convertible to a boolean, maintaining traditional Python `py::callable()` syntax
    alongside extended C++ using template parameters and function pointers. */
    template <typename Func, typename... Args>
    class CallTraits {
        const Func& func;

        struct NoReturn {};

        template <typename T, typename = void>
        struct Check {
            using type = NoReturn;
        };

        template <typename T>
        struct Check<
            T,
            std::void_t<decltype(std::declval<T>()(std::declval<Args>()...))>
        > {
            using type = decltype(std::declval<T>()(std::declval<Args>()...));
        };

        template <typename T, typename = void>
        struct OverloadsCallable : std::false_type {};
        template <typename T>
        struct OverloadsCallable<T, std::void_t<decltype(T::operator())>> :
            std::true_type
        {};

        template <typename T>
        static constexpr bool is_callable_any = std::disjunction_v<
            std::is_function<std::remove_pointer_t<T>>,
            std::is_member_function_pointer<std::decay_t<T>>,
            OverloadsCallable<std::decay_t<T>>
        >;

    public:
        constexpr CallTraits(const Func& func) : func(func) {}

        using Return = typename Check<Func>::type;
        static constexpr bool invalid = std::is_same_v<Return, NoReturn>;

        template <
            typename T = Func,
            std::enable_if_t<!detail::is_pyobject<T>::value, int> = 0
        >
        inline constexpr operator bool() const {
            if constexpr (sizeof...(Args) == 0) {
                return is_callable_any<Func>;
            } else {
                return std::is_invocable_v<Func, Args...>;
            }
        }

        template <
            typename T = Func,
            std::enable_if_t<detail::is_pyobject<T>::value, int> = 0
        >
        inline operator bool() const {
            if constexpr (sizeof...(Args) == 0) {
                return PyCallable_Check(func.ptr());
            } else if constexpr(std::is_same_v<Return, NoReturn>) {
                return false;
            } else {
                Function f(func);
                if ((f.n_args() - f.defaults().size()) != sizeof...(Args)) {
                    return false;
                }
                return true;
            }
        }

        friend std::ostream& operator<<(std::ostream& os, const CallTraits& traits) {
            if (traits) {
                os << "True";
            } else {
                os << "False";
            }
            return os;
        }

    };

}


/* Equivalent to Python `callable(obj)`, except that it supports extended C++ syntax to
allow compatibility with C++, checking against particular argument types.

Here's how this function can be used:

    if (py::callable(func)) {
        // func is a Python or C++ callable with any number of arguments.  This is
        // identical to the original Python `callable()` function.
    }

    if (py::callable<int, int>(func)) {
        // func is a C++ callable that can take two integers, or a Python callable
        // where each argument is convertible to a Python object, and the function
        // accepts at least two positional arguments after accounting for default
        // values.  Note that variable positional arguments are not accounted for, only
        // basic positional or positional-only arguments.
    }

    // gets the return type of the function, or an internal NoReturn placeholder if the
    // function is not callable with the given arguments.
    using Return = typename decltype(py::callable<int, int>(func))::Return;

    if constexpr (decltype(py::callable<int, int>(func))::invalid) {
        // used to disambiguate the `using Return = ...` line in the NoReturn case
    }

Various permutations of these examples are possible, allowing users to dynamically
dispatch to different functions based on their signature.
*/
template <typename... Args, typename Func>
inline constexpr auto callable(const Func& func) {
    return impl::CallTraits<Func, Args...>{func};
}


}  // namespace python
}  // namespace bertrand


/* User-defined literal to make embedding python scripts as easy as possible */
inline bertrand::py::Code operator"" _python(const char* source, size_t size) {
    return bertrand::py::Code(std::string_view(source, size));
}


namespace bertrand{
namespace py{
namespace impl {

    /* Custom atexit method that avoids conflicts with the Python interpreter when
    py::Code objects are stored as static variables.  This is always invoked
    immediately before Python exits, ensuring that all code objects are properly
    freed without causing memory access errors. */
    static void clean_up_living_code_objects() {
        for (Code* code : impl::LIVING_CODE_OBJECTS) {
            code->~Code();
        }
    }

    /* Register the atexit function from a Python context.  NOTE: we can't use
    Py_AtExit() here because it actually gets executed AFTER the python interpreter
    has already started finalizing.  If we register the cleanup function from a Python
    context, however, then we can guarantee that the Python interpreter is valid while
    we clean up each object.  This is convoluted, but it works. */
    static const bool CLEAN_UP_LIVING_CODE_OBJECTS = []() {
        if (Py_IsInitialized()) {
            R"(
                import atexit
                atexit.register(CLEANUP)
            )"_python({{"CLEANUP", py::Function(clean_up_living_code_objects)}});
            return true;
        }
        return false;
    }();

}
}
}


#endif  // BERTRAND_PYTHON_FUNC_H
