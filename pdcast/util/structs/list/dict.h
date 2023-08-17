// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_DICT_H
#define BERTRAND_STRUCTS_DICT_H

#include <cstddef>  // for size_t
#include <utility>  // std::pair
#include <variant>  // std::variant
#include <Python.h>  // CPython API

// Algorithms
#include "algorithms/append.h"
#include "algorithms/compare.h"
#include "algorithms/contains.h"
#include "algorithms/count.h"
#include "algorithms/delete_slice.h"
#include "algorithms/extend.h"
#include "algorithms/get_slice.h"
#include "algorithms/index.h"
#include "algorithms/insert.h"
#include "algorithms/move.h"
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
all linked dictionaries. */
using VariantDictView = std::variant<
    DictView<SingleNode, DirectAllocator>,
    DictView<SingleNode, FreeListAllocator>,
    DictView<SingleNode, PreAllocator>,
    DictView<DoubleNode, DirectAllocator>,
    DictView<DoubleNode, FreeListAllocator>,
    DictView<DoubleNode, PreAllocator>
>;


//////////////////////
////    PUBLIC    ////
//////////////////////


class VariantDict {
public:

    /* Construct a new VariantDict from an existing DictView.  This is called to
    construct a new `VariantDict` from the output of `DictView.copy()` or
    `get_slice()`. */
    template <typename NodeType, template <typename> class Allocator>
    VariantDict(DictView<NodeType, Allocator>&& view) {
        using Node = typename DictView<NodeType, Allocator>::Node;
        _doubly_linked = is_doubly_linked<Node>::value;
        view_variant = std::move(view);
    }

    /* Construct an empty DictView to match the given template parameters and
    wrap it as a VariantDict. This is called during `LinkedSet.__init__()`. */
    VariantDict(bool doubly_linked, ssize_t max_size) : _doubly_linked(doubly_linked) {
        if (doubly_linked) {
            if (max_size < 0) {
                view_variant = DictView<DoubleNode, FreeListAllocator>(max_size);
            } else {
                view_variant = DictView<DoubleNode, PreAllocator>(max_size);
            }
        } else {
            if (max_size < 0) {
                view_variant = DictView<SingleNode, FreeListAllocator>(max_size);
            } else {
                view_variant = DictView<SingleNode, PreAllocator>(max_size);
            }
        }
    }

    /* Construct a new ListView to match the given parameters and wrap it as a
    VariantDict. This is called during `LinkedSet.__init__()`. */
    VariantDict(
        PyObject* iterable,
        bool doubly_linked,
        bool reverse,
        ssize_t max_size,
        PyObject* spec
    ) : _doubly_linked(doubly_linked) {
        if (doubly_linked) {
            if (max_size < 0) {
                view_variant = DictView<DoubleNode, FreeListAllocator>(
                    iterable, reverse, max_size, spec
                );
            } else {
                view_variant = DictView<DoubleNode, PreAllocator>(
                    iterable, reverse, max_size, spec
                );
            }
        } else {
            if (max_size < 0) {
                view_variant = DictView<SingleNode, FreeListAllocator>(
                    iterable, reverse, max_size, spec
                );
            } else {
                view_variant = DictView<SingleNode, PreAllocator>(
                    iterable, reverse, max_size, spec
                );
            }
        }
    }

    //////////////////////////////
    ////    LIST INTERFACE    ////
    //////////////////////////////




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
    VariantDictView view_variant;
    bool _doubly_linked
}



#endif // BERTRAND_STRUCTS_DICT_H include guard
