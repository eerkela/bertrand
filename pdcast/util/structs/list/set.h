
// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_SET_H
#define BERTRAND_STRUCTS_SET_H

#include <cstddef>  // for size_t
#include <utility>  // std::pair
#include <variant>  // std::variant
#include <Python.h>  // CPython API

// Algorithms
#include "algorithms/append.h"
#include "algorithms/contains.h"
#include "algorithms/count.h"
#include "algorithms/delete_slice.h"
#include "algorithms/extend.h"
#include "algorithms/get_slice.h"
#include "algorithms/index.h"
#include "algorithms/insert.h"
#include "algorithms/pop.h"
#include "algorithms/remove.h"
#include "algorithms/reverse.h"
#include "algorithms/rotate.h"
#include "algorithms/set_slice.h"
#include "algorithms/sort.h"

// Core
#include "core/allocate.h"  // Allocator policies
#include "core/bounds.h"  // normalize_index(), normalize_bounds(), etc.
#include "core/node.h"  // Nodes
#include "core/view.h"  // Views


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Using a `std::variant` allows us to expose only a single Cython wrapper for
all linked lists. */
using VariantSetView = std::variant<
    SetView<SingleNode, DirectAllocator>,
    SetView<SingleNode, FreeListAllocator>,
    SetView<SingleNode, PreAllocator>,
    SetView<DoubleNode, DirectAllocator>,
    SetView<DoubleNode, FreeListAllocator>,
    SetView<DoubleNode, PreAllocator>
>;


//////////////////////
////    PUBLIC    ////
//////////////////////


class VariantSet {
public:

    /* Construct a new VariantSet from an existing SetView.  This is called to
    construct a new `VariantSet` from the output of `SetView.copy()` or
    `get_slice()`. */
    template <typename NodeType, template <typename> class Allocator>
    VariantSet(SetView<NodeType, Allocator>&& view) {
        using Node = typename SetView<NodeType, Allocator>::Node;
        _doubly_linked = is_doubly_linked<Node>::value;
        view_variant = std::move(view);
    }

    /* Construct an empty SetView to match the given template parameters and
    wrap it as a VariantSet. This is called during `LinkedSet.__init__()`. */
    VariantSet(bool doubly_linked, ssize_t max_size) : _doubly_linked(doubly_linked) {
        if (doubly_linked) {
            if (max_size < 0) {
                view_variant = SetView<DoubleNode, FreeListAllocator>(max_size);
            } else {
                view_variant = SetView<DoubleNode, PreAllocator>(max_size);
            }
        } else {
            if (max_size < 0) {
                view_variant = SetView<SingleNode, FreeListAllocator>(max_size);
            } else {
                view_variant = SetView<SingleNode, PreAllocator>(max_size);
            }
        }
    }

    /* Construct a new ListView to match the given parameters and wrap it as a
    VariantSet. This is called during `LinkedSet.__init__()`. */
    VariantSet(
        PyObject* iterable,
        bool doubly_linked,
        bool reverse,
        ssize_t max_size,
        PyObject* spec
    ) : _doubly_linked(doubly_linked) {
        if (doubly_linked) {
            if (max_size < 0) {
                view_variant = SetView<DoubleNode, FreeListAllocator>(
                    iterable, reverse, max_size, spec
                );
            } else {
                view_variant = SetView<DoubleNode, PreAllocator>(
                    iterable, reverse, max_size, spec
                );
            }
        } else {
            if (max_size < 0) {
                view_variant = SetView<SingleNode, FreeListAllocator>(
                    iterable, reverse, max_size, spec
                );
            } else {
                view_variant = SetView<SingleNode, PreAllocator>(
                    iterable, reverse, max_size, spec
                );
            }
        }
    }

    //////////////////////////////
    ////    LIST INTERFACE    ////
    //////////////////////////////

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
    template <typename T>
    inline void insert(T index, PyObject* item) {
        std::visit(
            [&](auto& view) {
                // allow Python-style negative indexing + boundschecking
                size_t norm_index = normalize_index(index, view.size, true);
                Ops::insert(&view, norm_index, item);
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
    template <typename T>
    inline size_t index(PyObject* item, T start, T stop) {
        return std::visit(
            [&](auto& view) {
                // allow Python-style negative indexing + boundschecking
                std::pair<size_t, size_t> bounds = normalize_bounds(
                    start, stop, view.size, true
                );
                return Ops::index(&view, item, bounds.first, bounds.second);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of count() for each variant. */
    template <typename T>
    inline size_t count(PyObject* item, T start, T stop) {
        return std::visit(
            [&](auto& view) {
                // allow Python-style negative indexing + boundschecking
                std::pair<size_t, size_t> bounds = normalize_bounds(
                    start, stop, view.size, true
                );
                return Ops::count(&view, item, bounds.first, bounds.second);
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
    template <typename T>
    inline PyObject* pop(T index) {
        return std::visit(
            [&](auto& view) {
                // allow Python-style negative indexing + boundschecking
                size_t norm_index = normalize_index(index, view.size, false);
                return Ops::pop(&view, norm_index);
            },
            view_variant
        );
    }

    /* Call the variant's copy() method and wrap the result as another VariantSet. */
    inline VariantSet* copy() {
        return std::visit(
            [&](auto& view) -> VariantSet* {
                auto copied = view.copy();
                if (copied == nullptr) {
                    return nullptr;  // propagate Python errors
                }
                return new VariantSet(std::move(*copied));
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
    template <typename T>
    inline PyObject* get_index(T index) {
        return std::visit(
            [&](auto& view) {
                // allow Python-style negative indexing + boundschecking
                size_t norm_index = normalize_index(index, view.size, false);
                return Ops::get_index(&view, norm_index);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of get_slice() for each variant. */
    inline VariantSet* get_slice(Py_ssize_t start, Py_ssize_t stop, Py_ssize_t step) {
        return std::visit(
            [&](auto& view) -> VariantSet* {
                auto slice = Ops::get_slice(&view, start, stop, step);
                if (slice == nullptr) {
                    return nullptr;  // propagate Python errors
                }
                return new VariantSet(std::move(*slice));
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of set_index() for each variant. */
    template <typename T>
    inline void set_index(T index, PyObject* value) {
        std::visit(
            [&](auto& view) {
                // allow Python-style negative indexing + boundschecking
                size_t norm_index = normalize_index(index, view.size, false);
                Ops::set_index(&view, norm_index, value);
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
    template <typename T>
    inline void delete_index(T index) {
        std::visit(
            [&](auto& view) {
                // allow Python-style negative indexing + boundschecking
                size_t norm_index = normalize_index(index, view.size, false);
                Ops::delete_index(&view, norm_index);
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

    /////////////////////////////
    ////    SET INTERFACE    ////
    /////////////////////////////

    /* Dispatch to the correct implementation of add() for each variant. */
    inline void add(PyObject* item, bool left) {
        std::visit(
            [&](auto& view) {
                Ops::append(&view, item, left);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of discard() for each variant. */
    inline void discard(PyObject* item) {
        std::visit(
            [&](auto& view) {
                Ops::discard(&view, item);
            },
            view_variant
        );
    }


    // TODO: intersect, difference, issubset, issuperset, isdisjoint, etc.


    ///////////////////////////////////
    ////    RELATIVE OPERATIONS    ////
    ///////////////////////////////////

    /* Dispatch to the correct implementation of insertafter() for each variant. */
    inline void insertafter(PyObject* sentinel, PyObject* item) {
        std::visit(
            [&](auto& view) {
                Ops::insertafter(&view, sentinel, item);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of insertbefore() for each variant. */
    inline void insertbefore(PyObject* sentinel, PyObject* item) {
        std::visit(
            [&](auto& view) {
                Ops::insertbefore(&view, sentinel, item);
            },
            view_variant
        );
    }    

    /* Dispatch to the correct implementation of extendafter() for each variant. */
    inline void extendafter(PyObject* sentinel, PyObject* items) {
        std::visit(
            [&](auto& view) {
                Ops::extendafter(&view, sentinel, items);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of extendbefore() for each variant. */
    inline void extendbefore(PyObject* sentinel, PyObject* items) {
        std::visit(
            [&](auto& view) {
                Ops::extendbefore(&view, sentinel, items);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of discardafter() for each variant. */
    inline void discardafter(PyObject* sentinel, PyObject* item) {
        std::visit(
            [&](auto& view) {
                Ops::discardafter(&view, sentinel, item);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of discardbefore() for each variant. */
    inline void discardbefore(PyObject* sentinel, PyObject* item) {
        std::visit(
            [&](auto& view) {
                Ops::discardbefore(&view, sentinel, item);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of popafter() for each variant. */
    inline void popafter(PyObject* sentinel, PyObject* item) {
        std::visit(
            [&](auto& view) {
                Ops::popafter(&view, sentinel, item);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of popbefore() for each variant. */
    inline void popbefore(PyObject* sentinel, PyObject* item) {
        std::visit(
            [&](auto& view) {
                Ops::popbefore(&view, sentinel, item);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of clearafter() for each variant. */
    inline void clearafter(PyObject* sentinel, PyObject* item) {
        std::visit(
            [&](auto& view) {
                Ops::clearafter(&view, sentinel, item);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of clearbefore() for each variant. */
    inline void clearbefore(PyObject* sentinel, PyObject* item) {
        std::visit(
            [&](auto& view) {
                Ops::clearbefore(&view, sentinel, item);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of move() for each variant. */
    template <typename T>
    inline void move(PyObject* item, T index) {
        std::visit(
            [&](auto& view) {
                // allow Python-style negative indexing + boundschecking
                size_t norm_index = normalize_index(index, view.size, true);
                Ops::move(&view, item, norm_index);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of moveright() for each variant. */
    inline void moveright(PyObject* item, ssize_t steps) {
        std::visit(
            [&](auto& view) {
                Ops::moveright(&view, item, steps);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of moveleft() for each variant. */
    inline void moveleft(PyObject* item, ssize_t steps) {
        std::visit(
            [&](auto& view) {
                Ops::moveleft(&view, item, steps);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of moveafter() for each variant. */
    inline void moveafter(PyObject* sentinel, PyObject* item) {
        std::visit(
            [&](auto& view) {
                Ops::moveafter(&view, sentinel, item);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of movebefore() for each variant. */
    inline void movebefore(PyObject* sentinel, PyObject* item) {
        std::visit(
            [&](auto& view) {
                Ops::movebefore(&view, sentinel, item);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of edge() for each variant. */
    inline void edge(PyObject* item1, PyObject* item2) {
        std::visit(
            [&](auto& view) {
                Ops::edge(&view, item1, item2);
            },
            view_variant
        );
    }

    /* Dispatch to the correct implementation of swap() for each variant. */
    inline void swap(PyObject* item1, PyObject* item2) {
        std::visit(
            [&](auto& view) {
                Ops::swap(&view, item1, item2);
            },
            view_variant
        );
    }

    /////////////////////////////////
    ////    ITERATOR PROTOCOL    ////
    /////////////////////////////////

    /* Check if the underlying view is doubly-linked. */
    inline bool doubly_linked() const {
        return _doubly_linked;
    }

    /* Get the head node of a singly-linked list. */
    inline SingleNode* get_head_single() {
        return std::visit(
            [&](auto& view) -> SingleNode* {
                using View = std::decay_t<decltype(view)>;
                using Node = typename View::Node;

                if constexpr (is_doubly_linked<Node>::value) {
                    throw std::runtime_error("List is not singly-linked.");
                } else {
                    return static_cast<SingleNode*>(view.head);
                }
            },
            view_variant
        );
    }

    /* Get the tail node of a singly-linked list. */
    inline SingleNode* get_tail_single() {
        return std::visit(
            [&](auto& view) -> SingleNode* {
                using View = std::decay_t<decltype(view)>;
                using Node = typename View::Node;

                if constexpr (is_doubly_linked<Node>::value) {
                    throw std::runtime_error("List is not singly-linked.");
                } else {
                    return static_cast<SingleNode*>(view.tail);
                }
            },
            view_variant
        );
    }

    /* Get the head node of a doubly-linked list. */
    inline DoubleNode* get_head_double() {
        return std::visit(
            [&](auto& view) -> DoubleNode* {
                using View = std::decay_t<decltype(view)>;
                using Node = typename View::Node;

                if constexpr (is_doubly_linked<Node>::value) {
                    return static_cast<DoubleNode*>(view.head);
                } else {
                    throw std::runtime_error("List is not doubly-linked.");
                }
            },
            view_variant
        );
    }

    /* Get the tail node of a doubly-linked list. */
    inline DoubleNode* get_tail_double() {
        return std::visit(
            [&](auto& view) -> DoubleNode* {
                using View = std::decay_t<decltype(view)>;
                using Node = typename View::Node;

                if constexpr (is_doubly_linked<Node>::value) {
                    return static_cast<DoubleNode*>(view.tail);
                } else {
                    throw std::runtime_error("List is not doubly-linked.");
                }
            },
            view_variant
        );
    }

private:
    VariantSetView view_variant;
    bool _doubly_linked
};


#endif  // BERTRAND_STRUCTS_SET_H include guard
