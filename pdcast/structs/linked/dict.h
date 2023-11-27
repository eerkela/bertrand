// include guard: BERTRAND_STRUCTS_LINKED_DICT_H
#ifndef BERTRAND_STRUCTS_LINKED_DICT_H
#define BERTRAND_STRUCTS_LINKED_DICT_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <ostream>  // std::ostream
#include <sstream>  // std::ostringstream
#include <utility>  // std::pair
#include <variant>  // std::variant
#include <Python.h>  // CPython API
#include "../util/args.h"  // PyArgs
#include "../util/except.h"  // throw_python()
#include "core/view.h"  // DictView
#include "base.h"  // LinkedBase
#include "list.h"  // PyListInterface
#include "set.h"  // PySetInterface

#include "algorithms/add.h"
#include "algorithms/contains.h"
#include "algorithms/count.h"
#include "algorithms/discard.h"
#include "algorithms/distance.h"
#include "algorithms/index.h"
#include "algorithms/insert.h"
#include "algorithms/move.h"
#include "algorithms/pop.h"
#include "algorithms/position.h"
// #include "algorithms/relative.h"
#include "algorithms/remove.h"
#include "algorithms/repr.h"
#include "algorithms/reverse.h"
#include "algorithms/rotate.h"
#include "algorithms/set_compare.h"
#include "algorithms/slice.h"
#include "algorithms/sort.h"
#include "algorithms/swap.h"
#include "algorithms/union.h"
#include "algorithms/update.h"


namespace bertrand {
namespace structs {
namespace linked {


// TODO: account for mapped type in template definition:
// LinkedDict<int, float, ...>



/* A n ordered dictionary based on a combined linked list and hash table. */
template <typename NodeType, typename LockPolicy = util::BasicLock>
class LinkedDict : public LinkedBase<linked::DictView<NodeType>, LockPolicy> {
    using Base = LinkedBase<linked::DictView<NodeType>, LockPolicy>;

public:
    using View = linked::DictView<NodeType>;
    using Node = typename View::Node;
    using Value = typename View::Value;
    using MappedValue = typename View::MappedValue;

    template <linked::Direction dir>
    using Iterator = typename View::template Iterator<dir>;
    template <linked::Direction dir>
    using ConstIterator = typename View::template ConstIterator<dir>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    // inherit constructors from LinkedBase
    using Base::Base;
    using Base::operator=;

    //////////////////////////////
    ////    DICT INTERFACE    ////
    //////////////////////////////


    ///////////////////////
    ////    PROXIES    ////
    ///////////////////////


    //////////////////////////////////
    ////    OPERATOR OVERLOADS    ////
    //////////////////////////////////


};


/////////////////////////////////////
////    STRING REPRESENTATION    ////
/////////////////////////////////////



//////////////////////////////
////    PYTHON WRAPPER    ////
//////////////////////////////


/* CRTP mixin class containing the Python dict interface for a linked data structure. */
template <typename Derived>
class PyDictInterface {

};


/* A discriminated union of templated `LinkedDict` types that can be used from
Python. */
class PyLinkedDict :
    public PyLinkedBase<PyLinkedDict>,
    public PyListInterface<PyLinkedDict>,
    public PySetInterface<PyLinkedDict>,
    public PyDictInterface<PyLinkedDict>
{

};


/* Python module definition. */
static struct PyModuleDef module_dict = {
    PyModuleDef_HEAD_INIT,
    .m_name = "dict",
    .m_doc = (
        "This module contains an optimized LinkedDict data structure for use "
        "in Python.  The exact same data structure is also available in C++ "
        "under the same header path (bertrand/structs/linked/dict.h)."
    ),
    .m_size = -1,
};


/* Python import hook. */
PyMODINIT_FUNC PyInit_dict(void) {
    // initialize type objects
    if (PyType_Ready(&PyLinkedDict::Type) < 0) return nullptr;

    // initialize module
    PyObject* mod = PyModule_Create(&module_dict);
    if (mod == nullptr) return nullptr;

    // link type to module
    Py_INCREF(&PyLinkedDict::Type);
    if (PyModule_AddObject(mod, "LinkedDict", (PyObject*) &PyLinkedDict::Type) < 0) {
        Py_DECREF(&PyLinkedDict::Type);
        Py_DECREF(mod);
        return nullptr;
    }
    return mod;
}


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_DICT_H
