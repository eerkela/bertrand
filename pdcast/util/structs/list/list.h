// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_LIST_H
#define BERTRAND_STRUCTS_LIST_H

#include <cstddef>  // for size_t
#include <optional>  // std::optional
#include <utility>  // std::pair
#include <variant>  // std::variant
#include <Python.h>  // CPython API

// Algorithms
#include "algorithms/append.h"
#include "algorithms/contains.h"
#include "algorithms/count.h"
#include "algorithms/delete.h"
#include "algorithms/extend.h"
#include "algorithms/get.h"
#include "algorithms/index.h"
#include "algorithms/insert.h"
#include "algorithms/pop.h"
#include "algorithms/remove.h"
#include "algorithms/reverse.h"
#include "algorithms/rotate.h"
#include "algorithms/set.h"
#include "algorithms/sort.h"

// Core
#include "core/allocate.h"  // Allocator policies
#include "core/node.h"  // Nodes
#include "core/view.h"  // Views


///////////////////////
////    PRIVATE    ////
///////////////////////


/* Using a `std::variant` allows us to expose only a single Cython wrapper for
all linked lists. */
using VariantView = std::variant<
    ListView<SingleNode, DirectAllocator>,
    ListView<SingleNode, FreeListAllocator>,
    ListView<SingleNode, PreAllocator>,
    ListView<DoubleNode, DirectAllocator>,
    ListView<DoubleNode, FreeListAllocator>,
    ListView<DoubleNode, PreAllocator>
    // SetView<SingleNode, DirectAllocator>,
    // SetView<SingleNode, FreeListAllocator>,
    // SetView<SingleNode, PreAllocator>,
    // SetView<DoubleNode, DirectAllocator>,
    // SetView<DoubleNode, FreeListAllocator>,
    // SetView<DoubleNode, PreAllocator>
    // DictView<SingleNode, DirectAllocator>,
    // DictView<SingleNode, FreeListAllocator>,
    // DictView<SingleNode, PreAllocator>,
    // DictView<DoubleNode, DirectAllocator>,
    // DictView<DoubleNode, FreeListAllocator>,
    // DictView<DoubleNode, PreAllocator>
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

    /* Construct a new VariantList from an existing view.  This is called to
    construct a new `VariantList` from the output of `view.copy()` or `get_slice()`. */
    template <typename View>
    VariantList(View&& view) : self(nullptr) {
        _doubly_linked = has_prev<typename View::Node>::value;
        variant = std::move(view);
    }

    /* Construct an empty ListView to match the given template parameters.  This
    is called to construct a LinkedList from an initializer sequence. */
    VariantList(bool doubly_linked, Py_ssize_t max_size, PyObject* spec) :
        _doubly_linked(doubly_linked), self(nullptr)
    {
        if (doubly_linked) {
            if (max_size < 0) {
                variant = ListView<DoubleNode, DynamicAllocator>(max_size, spec);
            } else {
                variant = ListView<DoubleNode, FixedAllocator>(max_size, spec);
            }
        } else {
            if (max_size < 0) {
                variant = ListView<SingleNode, DynamicAllocator>(max_size, spec);
            } else {
                variant = ListView<SingleNode, FixedAllocator>(max_size, spec);
            }
        }
    }

    /* Construct a new ListView to match the given parameters and wrap it as a
    VariantList. This is called during `LinkedList.__init__()`. */
    VariantList(
        PyObject* iterable,
        bool doubly_linked,
        bool reverse,
        Py_ssize_t max_size,
        PyObject* spec
    ) : _doubly_linked(doubly_linked), self(nullptr)
    {
        if (doubly_linked) {
            if (max_size < 0) {
                variant = ListView<DoubleNode, DynamicAllocator>(
                    iterable, reverse, max_size, spec
                );
            } else {
                variant = ListView<DoubleNode, FixedAllocator>(
                    iterable, reverse, max_size, spec
                );
            }
        } else {
            if (max_size < 0) {
                variant = ListView<SingleNode, DynamicAllocator>(
                    iterable, reverse, max_size, spec
                );
            } else {
                variant = ListView<SingleNode, FixedAllocator>(
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
            variant
        );
    }

    /* Dispatch to the correct implementation of insert() for each variant. */
    template <typename T>
    inline void insert(T index, PyObject* item) {
        std::visit(
            [&](auto& view) {
                Ops::insert(&view, index, item);
            },
            variant
        );
    }

    /* Dispatch to the correct implementation of extend() for each variant. */
    inline void extend(PyObject* items, bool left) {
        std::visit(
            [&](auto& view) {
                Ops::extend(&view, items, left);
            },
            variant
        );
    }

    /* Dispatch to the correct implementation of index() for each variant. */
    template <typename T>
    inline size_t index(PyObject* item, T start, T stop) {
        return std::visit(
            [&](auto& view) {
                return Ops::index(&view, item, start, stop);
            },
            variant
        );
    }

    /* Dispatch to the correct implementation of count() for each variant. */
    template <typename T>
    inline size_t count(PyObject* item, T start, T stop) {
        return std::visit(
            [&](auto& view) {
                return Ops::count(&view, item, start, stop);
            },
            variant
        );
    }

    /* Dispatch to the correct implementation of remove() for each variant. */
    inline void remove(PyObject* item) {
        std::visit(
            [&](auto& view) {
                Ops::remove(&view, item);
            },
            variant
        );
    }

    /* Dispatch to the correct implementation of pop() for each variant. */
    template <typename T>
    inline PyObject* pop(T index) {
        return std::visit(
            [&](auto& view) {
                return Ops::pop(&view, index);
            },
            variant
        );
    }

    /* Call the variant's copy() method and wrap the result as another VariantList. */
    inline VariantList* copy() {
        return std::visit(
            [&](auto& view) -> VariantList* {
                auto copied = view.copy();
                if (copied == nullptr) {
                    return nullptr;  // propagate Python errors
                }
                return new VariantList(std::move(*copied));
            },
            variant
        );
    }

    /* Call the variant's clear() method. */
    inline void clear() {
        std::visit(
            [&](auto& view) {
                view.clear();
            },
            variant
        );
    }

    /* Dispatch to the correct implementation of sort() for each variant. */
    inline void sort(PyObject* key, bool reverse) {
        std::visit(
            [&](auto& view) {
                Ops::sort(&view, key, reverse);
            },
            variant
        );
    }

    /* Dispatch to the correct implementation of reverse() for each variant. */
    inline void reverse() {
        std::visit(
            [&](auto& view) {
                Ops::reverse(&view);
            },
            variant
        );
    }

    /* Dispatch to the correct implementation of rotate() for each variant. */
    inline void rotate(Py_ssize_t steps) {
        std::visit(
            [&](auto& view) {
                Ops::rotate(&view, steps);
            },
            variant
        );
    }

    /* Get the number of elements in the view. */
    inline size_t size() {
        return std::visit(
            [&](auto& view) {
                return view.size;
            },
            variant
        );
    }

    /* Dispatch to the correct implementation of get_index() for each variant. */
    template <typename T>
    inline PyObject* get_index(T index) {
        return std::visit(
            [&](auto& view) {
                return Ops::get_index(&view, index);
            },
            variant
        );
    }

    /* Dispatch to the correct implementation of get_slice() for each variant. */
    inline VariantList* get_slice(Py_ssize_t start, Py_ssize_t stop, Py_ssize_t step) {
        return std::visit(
            [&](auto& view) -> VariantList* {
                auto slice = Ops::get_slice(&view, start, stop, step);
                if (slice == nullptr) {
                    return nullptr;  // propagate Python errors
                }
                return new VariantList(std::move(*slice));
            },
            variant
        );
    }

    /* Dispatch to the correct implementation of set_index() for each variant. */
    template <typename T>
    inline void set_index(T index, PyObject* value) {
        std::visit(
            [&](auto& view) {
                Ops::set_index(&view, index, value);
            },
            variant
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
            variant
        );
    }

    /* Dispatch to the correct implementation of delete_index() for each variant. */
    template <typename T>
    inline void delete_index(T index) {
        std::visit(
            [&](auto& view) {
                Ops::delete_index(&view, index);
            },
            variant
        );
    }

    /* Dispatch to the correct implementation of delete_slice() for each variant. */
    inline void delete_slice(Py_ssize_t start, Py_ssize_t stop, Py_ssize_t step) {
        std::visit(
            [&](auto& view) {
                Ops::delete_slice(&view, start, stop, step);
            },
            variant
        );
    }

    /* Dispatch to the correct implementation of contains() for each variant. */
    inline int contains(PyObject* item) {
        return std::visit(
            [&](auto& view) {
                return Ops::contains(&view, item);
            },
            variant
        );
    }

    /* A proxy object that weakly references a VariantList and exposes additional
    methods for efficient operations on slices within the list. */
    class SliceProxy {
    public:

        /* Construct a new SliceProxy from a VariantList and a slice. */
        SliceProxy(
            std::weak_ptr<VariantList> variant,
            long long start,
            long long stop,
            long long step
        ) : variant(variant), start(start), stop(stop), step(step)
        {}

        /* Dispatch to the correct implementation of Slice::get() for each variant. */
        VariantList* get_() {
            // get strong reference to variant
            auto ref = strong_ref();
            if (ref == nullptr) {
                return nullptr;  // propagate
            }

            // dispatch to proxy
            return std::visit(
                [&](auto& view) -> VariantList* {
                    auto proxy = view.slice(start, stop, step);
                    if (!proxy.has_value()) {
                        return nullptr;  // propagate errors
                    }
                    auto result = Slice::get_(&(proxy.value()));
                    if (!result.has_value()) {
                        return nullptr;  // propagate errors
                    }
                    return new VariantList(std::move(result.value()));
                },
                ref->variant
            );
        }

        // /* Dispatch to the correct implementation of Slice::set() for each variant. */
        // void set_(PyObject* items) {
        //     // get strong reference to variant
        //     auto ref = strong_ref();
        //     if (ref == nullptr) {
        //         return;  // propagate
        //     }

        //     // dispatch to proxy
        //     std::visit(
        //         [&](auto& view) {
        //             auto proxy = view.slice(start, stop, step);
        //             proxy.execute(Slice::set, items);
        //         },
        //         ref->variant
        //     );
        // }

        /* Dispatch to the correct implementation of Slice::del() for each variant. */
        void delete_() {
            // get strong reference to variant
            auto ref = strong_ref();
            if (ref == nullptr) {
                return;  // propagate
            }

            // dispatch to proxy
            std::visit(
                [&](auto& view) {
                    auto proxy = view.slice(start, stop, step);
                    if (!proxy.has_value()) {
                        return;  // propagate error
                    }
                    Slice::delete_(&(proxy.value()));
                },
                ref->variant
            );
        }

    private:
        std::weak_ptr<VariantList> variant;
        long long start;
        long long stop;
        long long step;

        /* Generate a strong reference to the VariantList. */
        std::shared_ptr<VariantList> strong_ref() {
            auto strong_ref = variant.lock();
            if (strong_ref == nullptr) {
                PyErr_SetString(
                    PyExc_ReferenceError,
                    "SliceProxy references a list that no longer exists"
                );
            }
            return strong_ref;
        }

    };

    /* Construct a SliceProxy for slice operations within a linked list. */
    inline SliceProxy slice(
        long long start,
        long long stop,
        long long step = 1
    ) {
        // lazily initialize self reference
        if (self == nullptr) {
            // NOTE: we use a custom deleter to prevent the shared_ptr from
            // double freeing the VariantList when it goes out of scope.
            self = std::shared_ptr<VariantList>(this, [](VariantList*) {});
        }
        return SliceProxy(std::weak_ptr<VariantList>(self), start, stop, step);
    }

    /////////////////////////////
    ////    EXTRA METHODS    ////
    /////////////////////////////

    /* Get the specialization of the variant view. */
    inline PyObject* get_specialization() {
        return std::visit(
            [&](auto& view) {
                PyObject* spec = view.specialization;
                if (spec != nullptr) {
                    Py_INCREF(spec);
                }
                return spec;
            },
            variant
        );
    }

    /* Set the specialization of the variant view. */
    inline void specialize(PyObject* spec) {
        std::visit(
            [&](auto& view) {
                view.specialize(spec);
            },
            variant
        );
    }

    /* Lock the list for use in a multithreaded context. */
    inline std::lock_guard<std::mutex> lock() {
        return std::visit(
            [&](auto& view) {
                return view.lock();
            },
            variant
        );
    }

    /* Lock the list for use in a multithreaded context. */
    inline std::lock_guard<std::mutex>* lock_context() {
        return std::visit(
            [&](auto& view) {
                return view.lock_context();
            },
            variant
        );
    }

    /* Get the amount of memory being consumed by the view. */
    inline size_t nbytes() {
        return std::visit(
            [&](auto& view) {
                return view.nbytes();
            },
            variant
        );
    }

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

                if constexpr (has_prev<Node>::value) {
                    throw std::runtime_error("List is not singly-linked.");
                } else {
                    return static_cast<SingleNode*>(view.head);
                }
            },
            variant
        );
    }

    /* Get the tail node of a singly-linked list. */
    inline SingleNode* get_tail_single() {
        return std::visit(
            [&](auto& view) -> SingleNode* {
                using View = std::decay_t<decltype(view)>;
                using Node = typename View::Node;

                if constexpr (has_prev<Node>::value) {
                    throw std::runtime_error("List is not singly-linked.");
                } else {
                    return static_cast<SingleNode*>(view.tail);
                }
            },
            variant
        );
    }

    /* Get the head node of a doubly-linked list. */
    inline DoubleNode* get_head_double() {
        return std::visit(
            [&](auto& view) -> DoubleNode* {
                using View = std::decay_t<decltype(view)>;
                using Node = typename View::Node;

                if constexpr (has_prev<Node>::value) {
                    return static_cast<DoubleNode*>(view.head);
                } else {
                    throw std::runtime_error("List is not doubly-linked.");
                }
            },
            variant
        );
    }

    /* Get the tail node of a doubly-linked list. */
    inline DoubleNode* get_tail_double() {
        return std::visit(
            [&](auto& view) -> DoubleNode* {
                using View = std::decay_t<decltype(view)>;
                using Node = typename View::Node;

                if constexpr (has_prev<Node>::value) {
                    return static_cast<DoubleNode*>(view.tail);
                } else {
                    throw std::runtime_error("List is not doubly-linked.");
                }
            },
            variant
        );
    }

protected:
    VariantView variant;
    bool _doubly_linked;
    std::shared_ptr<VariantList> self;  // allows weak references in proxies
};


#endif  // BERTRAND_STRUCTS_LIST_H include guard
