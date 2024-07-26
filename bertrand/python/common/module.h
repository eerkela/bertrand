#ifndef BERTRAND_PYTHON_COMMON_MODULE_H
#define BERTRAND_PYTHON_COMMON_MODULE_H

#include "declarations.h"
#include "except.h"
#include "ops.h"
#include "object.h"
#include "func.h"


// TODO: py::Module<"name"> should be reimplemented here.  A unique specialization
// must be created for each binding file, and declspec(property) attributes will be
// added to allow type-safe access to the module's contents, based on the value of
// __getattr__<Module<"name">, "attr">.  This should allow completely type-safe
// access to the module's contents, and will replace the pybind11 infrastructure for
// creating them.  The AST parser will emit a unique module type for each primary
// module interface, and the templated name will always match the exported module
// name.


// TODO: perhaps this whole class is written uniquely by the AST parser.


namespace py {


template <StaticStr Name>
class Module : public Object, public impl::ModuleTag {

    inline static PyModuleDef module_def = {
        .m_base = PyModuleDef_HEAD_INIT,
        .m_name = Name,
        .m_doc = "A Python wrapper around the '" + Name + "' C++ module.",
        .m_size = -1,
    };

public:

};


}


#endif  // BERTRAND_PYTHON_COMMON_MODULE_H
