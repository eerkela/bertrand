#ifndef BERTRAND_PYTHON_FUNC_H
#define BERTRAND_PYTHON_FUNC_H

#include "common.h"



namespace bertrand {
namespace py {


namespace impl {

    // PyTypeObject* PyMethod_Type = nullptr;  // provided in Python.h
    PyTypeObject* PyClassMethod_Type = nullptr;
    PyTypeObject* PyStaticMethod_Type = nullptr;
    // PyTypeObject* PyProperty_Type = nullptr;  // provided in Python.h?

} // namespace impl



/* A new subclass of pybind11::object that represents a compiled Python code object, as
returned by the built-in `compile()` function.

This class allows seamless embedding of Python as a scripting language within C++.  It
is extremely powerful, and is best explained by example:

    py::Code script(R"(
    import numpy as np
    print(np.arange(10))
    )");

    script();  // prints [0 1 2 3 4 5 6 7 8 9]

This creates an embedded Python script that can be executed as a function.  Here, the
script is stateless, and can be executed without context.  Most of the time, this won't
be the case, and data will need to be passed into the script to populate its namespace.
For instance:

    py::Code script(R"(
    print("Hello, " + name + "!")  # name is not defined in this context
    )");

If we try to execute this script without a context, we'll get an error:

    script();  // raises NameError: name 'name' is not defined

We can solve this by building a dictionary and passing it into the script:

    script({{"name", "world"}});  // prints Hello, world!

This uses any of the ordinary py::Dict constructors, which can take arbitrary C++ input
as long as a corresponding Python type exists, making it possible to seamlessly pass
data from C++ to Python.

In the previous example, the dictionary exists only for the duration of the script's
execution, and is discarded immediately afterwards.  However, it is also possible to
pass a mutable reference to an external dictionary, which will be updated in-place
during the script's execution.  This is useful for inspecting the state of the script
after it has been executed, and for passing data back from Python to C++.  For example:

    py::Code script(R"(
    x = 1
    y = 2
    z = 3
    )");

    py::Dict context;
    script(context);
    py::print(context);  // prints {"x": 1, "y": 2, "z": 3}

Users can then extract the values from the dictionary and use them in C++ as needed,
possibly converting them back into C++ types.  For example:

    int x = context["x"].cast<int>();

    int func(int y, int z) {
        return y + z;
    }

    int result = func(context["y"].cast<int>(), context["z"].cast<int>());

This makes it possible to not only pass C++ values into Python, but to also do the
inverse and extract Python values back into C++.  This is a powerful feature that
allows Python to be used as an inline scripting language in any C++ application, with
full native compatibility in both directions.  The contents of the script are evaluated
just like an ordinary Python file, so there are no restrictions on what can be done
inside it.  This includes importing modules, defining classes and functions to be
exported back to C++, interacting with third-party libraries, and so on. */
class Code :
    public pybind11::object,
    public impl::EqualCompare<Code>
{
    using Base = pybind11::object;
    using Compare = impl::EqualCompare<Code>;

    static PyObject* convert_to_code(PyObject* obj) {
        throw TypeError("cannot convert to code object");
    }

public:
    CONSTRUCTORS(Code, PyCode_Check, convert_to_code);

    /* Parse and compile a source string into a Python code object. */
    explicit Code(const char* source) : Base([&source] {
        PyObject* result = Py_CompileString(
            source,
            "<embedded C++ file>",
            Py_file_input
        );
        if (result == nullptr) {
            throw error_already_set();
        }
        return result;
    }(), stolen_t{}) {}

    /* Parse and compile a source string into a Python code object. */
    explicit Code(const std::string& source) : Code(source.c_str()) {}

    /* Parse and compile a source string into a Python code object. */
    explicit Code(const std::string_view& source) : Code(source.data()) {}

    ////////////////////////////////
    ////    PyCode_* METHODS    ////
    ////////////////////////////////

    /* Execute the code object without context. */
    inline void operator()() const {
        Dict context;
        PyObject* result = PyEval_EvalCode(this->ptr(), context.ptr(), context.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        Py_DECREF(result);  // always None
    }

    /* Execute the code object with the given context. */
    inline void operator()(Dict& context) const {
        PyObject* result = PyEval_EvalCode(this->ptr(), context.ptr(), context.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        Py_DECREF(result);  // always None
    }

    /* Execute the code object with the given context. */
    inline void operator()(Dict&& context) const {
        return (*this)(context);
    }

    /* Execute the code object with the given context, given as separate global and
    local namespaces. */
    inline void operator()(Dict& globals, Dict& locals) const {
        PyObject* result = PyEval_EvalCode(this->ptr(), globals.ptr(), locals.ptr());
        if (result == nullptr) {
            throw error_already_set();
        }
        Py_DECREF(result);  // always None
    }

    /* Execute the code object with the given context. */
    inline void operator()(Dict&& globals, Dict&& locals) const {
        return (*this)(globals, locals);
    }

    /////////////////////
    ////    SLOTS    ////
    /////////////////////

    /* A proxy for the code object's PyCodeObject* struct. */
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
        positional-only arguments and those with default values (but not
        keyword-only). */
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

        // /* Get a tuple containing the anmes of free variables in the function (i.e.
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
};

















/* Wrapper around a pybind11::Function that allows it to be constructed from a C++
lambda or function pointer, and enables extra introspection via the C API. */
class Function : public pybind11::function {
    using Base = pybind11::function;

public:
    using Base::Base;
    using Base::operator=;

    template <typename Func>
    explicit Function(Func&& func) : Base([&func] {
        return pybind11::cpp_function(std::forward<Func>(func));
    }()) {}

    ///////////////////////////////
    ////    PyFunction_ API    ////
    ///////////////////////////////

    // TODO: introspection tools: name(), code(), etc.

    // /* Get the name of the file from which the code was compiled. */
    // inline std::string filename() const {
    //     return code().filename();
    // }


    // /* Get the first line number of the function. */
    // inline size_t line_number() const noexcept {
    //     return code().line_number();
    // }

    // /* Get the function's base name. */
    // inline std::string name() const {
    //     return code().name();
    // }


    // /* Get the module that the function is defined in. */
    // inline std::optional<Module<Ref::BORROW>> module_() const {
    //     PyObject* mod = PyFunction_GetModule(this->obj);
    //     if (mod == nullptr) {
    //         return std::nullopt;
    //     } else {
    //         return std::make_optional(Module<Ref::BORROW>(module));
    //     }
    // }



};


/* New subclass of pybind11::object that represents a bound method at the Python
level. */
class Method : public pybind11::object {
    using Base = pybind11::object;

public:



};


/* New subclass of pybind11::object that represents a bound classmethod at the Python
level. */
class ClassMethod : public pybind11::object {
    using Base = pybind11::object;

public:



};


/* Wrapper around a pybind11::StaticMethod that allows it to be constructed from a
C++ lambda or function pointer, and enables extra introspection via the C API. */
class StaticMethod : public pybind11::staticmethod {
    using Base = pybind11::staticmethod;

public:



};


/* New subclass of pybind11::object that represents a property descriptor at the
Python level. */
class Property : public pybind11::object {
    using Base = pybind11::object;

public:



};


/* Equivalent to Python `callable(obj)`. */
inline bool callable(const pybind11::handle& obj) {
    return PyCallable_Check(obj.ptr());
}


}  // namespace python
}  // namespace bertrand


#endif  // BERTRAND_PYTHON_FUNC_H
