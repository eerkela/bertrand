#include "linked.h"


namespace bertrand {


/* bertrand.structs.linked.list module definition. */
static struct PyModuleDef pymodule_linked = {
    .m_base = PyModuleDef_HEAD_INIT,
    .m_name = "linked",
    .m_doc = (
        "This module contains a set of high-performance linked lists implemented "
        "in C++ and exposed to Python via the native CPython interface.  The "
        "exact same data structures are also available in C++ under the "
        "<bertrand.h> header and associated namespace."
    ),
    .m_size = -1,
};


/* Python import hook. */
PyMODINIT_FUNC PyInit_linked(void) {
    if (PyType_Ready(&PyLinkedList::Type) < 0) {
        return nullptr;
    }
    if (PyType_Ready(&PyLinkedSet::Type) < 0) {
        return nullptr;
    }
    if (PyType_Ready(&PyLinkedDict::Type) < 0) {
        return nullptr;
    }

    PyObject* mod = PyModule_Create(&pymodule_linked);
    if (mod == nullptr) {
        return nullptr;
    }

    Py_INCREF(&PyLinkedList::Type);
    if (PyModule_AddObject(mod, "LinkedList", (PyObject*) &PyLinkedList::Type) < 0) {
        Py_DECREF(mod);
        Py_DECREF(&PyLinkedList::Type);
        return nullptr;
    }
    Py_INCREF(&PyLinkedSet::Type);
    if (PyModule_AddObject(mod, "LinkedSet", (PyObject*) &PyLinkedSet::Type) < 0) {
        Py_DECREF(mod);
        Py_DECREF(&PyLinkedList::Type);
        Py_DECREF(&PyLinkedSet::Type);
        return nullptr;
    }
    Py_INCREF(&PyLinkedDict::Type);
    if (PyModule_AddObject(mod, "LinkedDict", (PyObject*) &PyLinkedDict::Type) < 0) {
        Py_DECREF(mod);
        Py_DECREF(&PyLinkedList::Type);
        Py_DECREF(&PyLinkedSet::Type);
        Py_DECREF(&PyLinkedDict::Type);
        return nullptr;
    }

    return mod;
}


}
