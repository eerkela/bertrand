
// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_LIST_H
#define BERTRAND_STRUCTS_LIST_H


#include <cstddef>  // for size_t
#include <variant>  // std::variant
#include <Python.h>  // CPython API
#include "node.h"  // Nodes + Allocators
#include "view.h"  // Views

// Algorithms
#include "append.h"
#include "contains.h"
#include "count.h"
#include "delete_slice.h"
#include "extend.h"
#include "get_slice.h"
#include "index.h"
#include "insert.h"
#include "pop.h"
#include "remove.h"
#include "reverse.h"
#include "rotate.h"
#include "set_slice.h"
#include "sort.h"


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Using a `std::variant` allows us to expose only a single Cython wrapper for
all linked lists. */
using ViewVariant = std::variant<
    ListView<SingleNode, DirectAllocator>,
    ListView<SingleNode, FreeListAllocator>,
    ListView<SingleNode, PreAllocator>,
    ListView<DoubleNode, DirectAllocator>,
    ListView<DoubleNode, FreeListAllocator>,
    ListView<DoubleNode, PreAllocator>
>;


//////////////////////
////    PUBLIC    ////
//////////////////////


// NOTE: If we did not use a variant here, we would have to implement a dozen
// or more different wrappers for each configuration of each data structure,
// each of which would be identical except for the type of its view.  This is a
// maintenance nightmare, and we would probably end up just wrapping everything
// in a separate Python layer to achieve a unified interface anyways.  By using
// a variant, we can the dispatch at the C++ level and avoid writing tons of
// boilerplate.  This also allows us to keep things statically typed as much as
// possible, which means no vtable lookups or other forms of indirection.


/* A class that binds the appropriate methods for the given view as a std::variant
of templated `ListView` types. */
class VariantList {
public:

    /* Construct a new VariantList from an existing ListView.  This is called
    to construct a new `VariantList` from the output of `ListView.copy()` or
    `get_slice()`. */
    template <typename NodeType, template <typename> class Allocator>
    VariantList(ListView<NodeType, Allocator>& view) {
        view_variant = view;
    }

    /* Construct an empty ListView to match the given template parameters and
    wrap it as a VariantList. This is called during `LinkedList.__init__()`. */
    VariantList(bool doubly_linked, ssize_t max_size = -1) {
        if (doubly_linked) {
            if (max_size < 0) {
                view_variant = ListView<DoubleNode, FreeListAllocator>(max_size);
            } else {
                view_variant = ListView<DoubleNode, PreAllocator>(max_size);
            }
        } else {
            if (max_size < 0) {
                view_variant = ListView<SingleNode, FreeListAllocator>(max_size);
            } else {
                view_variant = ListView<SingleNode, PreAllocator>(max_size);
            }
        }
    }

    /* Construct a new ListView to match the given parameters and wrap it as a
    VariantList. This is called during `LinkedList.__init__()`. */
    VariantList(
        PyObject* iterable,
        bool doubly_linked,
        bool reverse = false,
        PyObject* spec = nullptr,
        ssize_t max_size = -1
    ) {
        if (doubly_linked) {
            if (max_size < 0) {
                view_variant = ListView<DoubleNode, FreeListAllocator>(
                    iterable, reverse, spec, max_size
                );
            } else {
                view_variant = ListView<DoubleNode, PreAllocator>(
                    iterable, reverse, spec, max_size
                );
            }
        } else {
            if (max_size < 0) {
                view_variant = ListView<SingleNode, FreeListAllocator>(
                    iterable, reverse, spec, max_size
                );
            } else {
                view_variant = ListView<SingleNode, PreAllocator>(
                    iterable, reverse, spec, max_size
                );
            }
        }
    }

    /* Dispatch to the correct implementation of append() for each variant. */
    inline void append(PyObject* item, bool left) {
        std::visit(
            [&](auto& view) {
                Ops::append(&view, item, left);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of insert() for each variant. */
    inline void insert(size_t index, PyObject* item) {
        std::visit(
            [&](auto& view) {
                Ops::insert(&view, index, item);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of extend() for each variant. */
    inline void extend(PyObject* items, bool left) {
        std::visit(
            [&](auto& view) {
                Ops::extend(&view, items, left);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of index() for each variant. */
    inline size_t index(PyObject* item, size_t start, size_t stop) {
        return std::visit(
            [&](auto& view) {
                return Ops::index(&view, item, start, stop);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of count() for each variant. */
    inline size_t count(PyObject* item, size_t start, size_t stop) {
        return std::visit(
            [&](auto& view) {
                return Ops::count(&view, item, start, stop);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of remove() for each variant. */
    inline void remove(PyObject* item) {
        std::visit(
            [&](auto& view) {
                Ops::remove(&view, item);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of pop() for each variant. */
    inline PyObject* pop(size_t index) {
        return std::visit(
            [&](auto& view) {
                return Ops::pop(&view, index);
            },
            view_variant
        );
    }

    /* Call the variant's copy() method and wrap the result as another VariantList. */
    inline VariantList* copy() {
        return std::visit(
            [&](auto& view) {
                return new VariantList(view.copy());
            },
            view_variant
        );
    }

    /* Call the variant's clear() method. */
    inline void clear() {
        std::visit(
            [&](auto& view) {
                view.clear();
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of sort() for each variant. */
    inline void sort(PyObject* key, bool reverse) {
        std::visit(
            [&](auto& view) {
                Ops::sort(&view, key, reverse);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of reverse() for each variant. */
    inline void reverse() {
        std::visit(
            [&](auto& view) {
                Ops::reverse(&view);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of rotate() for each variant. */
    inline void rotate(ssize_t steps) {
        std::visit(
            [&](auto& view) {
                Ops::rotate(&view, steps);
            },
            view_variant
        );
    }

    /* Get the specialization of the variant view. */
    inline PyObject* get_specialization() {
        return std::visit(
            [&](auto& view) {
                return view.get_specialization();
            },
            view_variant
        );
    }

    /* Set the specialization of the variant view. */
    inline void specialize(PyObject* spec) {
        std::visit(
            [&](auto& view) {
                view.specialize(spec);
            },
            view_variant
        );
    }

    /* Get the amount of memory being consumed by the view. */
    inline size_t nbytes() {
        return std::visit(
            [&](auto& view) {
                return view.nbytes();
            },
            view_variant
        );
    }

    /* Get the number of elements in the view. */
    inline size_t size() {
        return std::visit(
            [&](auto& view) {
                return view.size;
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of get_index() for each variant. */
    inline PyObject* get_index(size_t index) {
        return std::visit(
            [&](auto& view) {
                return Ops::get_index(&view, index);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of get_slice() for each variant. */
    inline VariantList* get_slice(Py_ssize_t start, Py_ssize_t stop, Py_ssize_t step) {
        return std::visit(
            [&](auto& view) {
                return new VariantList(Ops::get_slice(&view, start, stop, step));
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of set_index() for each variant. */
    inline void set_index(size_t index, PyObject* value) {
        std::visit(
            [&](auto& view) {
                Ops::set_index(&view, index, value);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of set_slice() for each variant. */
    inline void set_slice(
        Py_ssize_t start,
        Py_ssize_t stop,
        Py_ssize_t step,
        PyObject* items
    ) {
        std::visit(
            [&](auto& view) {
                Ops::set_slice(&view, start, stop, step, items);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of delete_index() for each variant. */
    inline void delete_index(size_t index) {
        std::visit(
            [&](auto& view) {
                Ops::delete_index(&view, index);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of delete_slice() for each variant. */
    inline void delete_slice(Py_ssize_t start, Py_ssize_t stop, Py_ssize_t step) {
        std::visit(
            [&](auto& view) {
                Ops::delete_slice(&view, start, stop, step);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of contains() for each variant. */
    inline int contains(PyObject* item) {
        return std::visit(
            [&](auto& view) {
                return Ops::contains(&view, item);
            },
            view_variant
        );
    }

private:
    ViewVariant view_variant;
};



#endif  // BERTRAND_STRUCTS_LIST_H include guard
