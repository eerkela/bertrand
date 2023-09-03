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
    ListView<SingleNode, DynamicAllocator>,
    ListView<SingleNode, FixedAllocator>,
    ListView<DoubleNode, DynamicAllocator>,
    ListView<DoubleNode, FixedAllocator>,
    ListView<Hashed<SingleNode>, DynamicAllocator>,
    ListView<Hashed<SingleNode>, FixedAllocator>,
    ListView<Hashed<DoubleNode>, DynamicAllocator>,
    ListView<Hashed<DoubleNode>, FixedAllocator>,
    SetView<SingleNode, DynamicAllocator>,
    SetView<SingleNode, FixedAllocator>,
    SetView<DoubleNode, DynamicAllocator>,
    SetView<DoubleNode, FixedAllocator>
    // DictView<SingleNode, DynamicAllocator>,
    // DictView<SingleNode, FixedAllocator>,
    // DictView<DoubleNode, DynamicAllocator>,
    // DictView<DoubleNode, FixedAllocator>,
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

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

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

    /* Implement LinkedList.append() for all variants. */
    inline void append(PyObject* item, bool left) {
        std::visit([&](auto& view) { Ops::append(&view, item, left); }, variant);
    }

    /* Implement LinkedList.insert() for all variants. */
    template <typename T>
    inline void insert(T index, PyObject* item) {
        std::visit([&](auto& view) { Ops::insert(&view, index, item); }, variant);
    }

    /* Insert LinkedList.extend() for all variants. */
    inline void extend(PyObject* items, bool left) {
        std::visit([&](auto& view) { Ops::extend(&view, items, left); }, variant);
    }

    /* Implement LinkedList.index() for all variants. */
    template <typename T>
    inline size_t index(PyObject* item, T start, T stop) {
        return std::visit(
            [&](auto& view) {
                return Ops::index(&view, item, start, stop);
            },
            variant
        );
    }

    /* Implement LinkedList.count() for all variants. */
    template <typename T>
    inline size_t count(PyObject* item, T start, T stop) {
        return std::visit(
            [&](auto& view) {
                return Ops::count(&view, item, start, stop);
            },
            variant
        );
    }

    /* Implement LinkedList.remove() for all variants. */
    inline void remove(PyObject* item) {
        std::visit([&](auto& view) { Ops::remove(&view, item); }, variant);
    }

    /* Implement LinkedList.pop() for all variants. */
    template <typename T>
    inline PyObject* pop(T index) {
        return std::visit([&](auto& view) { return Ops::pop(&view, index); }, variant);
    }

    /* Implement LinkedList.copy() for all variants. */
    inline VariantList* copy() {
        return std::visit(
            [&](auto& view) -> VariantList* {
                // copy underlying view
                auto copied = view.copy();
                if (copied == nullptr) {
                    return nullptr;  // propagate Python errors
                }

                // wrap result as VariantList
                return new VariantList(std::move(*copied));
            },
            variant
        );
    }

    /* Implement LinkedList.clear() for all variants. */
    inline void clear() {
        std::visit([&](auto& view) { view.clear(); }, variant);
    }

    /* Implement LinkedList.sort() for all variants. */
    inline void sort(PyObject* key, bool reverse) {
        std::visit([&](auto& view) { Ops::sort(&view, key, reverse); }, variant);
    }

    /* Implement LinkedList.reverse() for all variants. */
    inline void reverse() {
        std::visit([&](auto& view) { Ops::reverse(&view); }, variant);
    }

    /* Implement LinkedList.rotate() for all variants. */
    inline void rotate(Py_ssize_t steps) {
        std::visit([&](auto& view) { Ops::rotate(&view, steps); }, variant);
    }

    /* Implement LinkedList.__len__() for all variants. */
    inline size_t size() {
        return std::visit([&](auto& view) { return view.size; }, variant);
    }

    /* Implement LinkedList.__contains__() for all variants. */
    inline int contains(PyObject* item) {
        return std::visit(
            [&](auto& view) {
                return Ops::contains(&view, item);
            },
            variant
        );
    }

    /* Implement LinkedList.__getitem__() for all variants (single index). */
    template <typename T>
    inline PyObject* get_index(T index) {
        return std::visit(
            [&](auto& view) {
                return Ops::get_index(&view, index);
            },
            variant
        );
    }

    /* Implement LinkedList.__setitem__() for all variants (single index). */
    template <typename T>
    inline void set_index(T index, PyObject* value) {
        std::visit([&](auto& view) { Ops::set_index(&view, index, value); }, variant);
    }

    // TODO: rewrite set_slice using new SliceProxy

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

    /* Implement LinkedList.__delitem__() for all variants (single index). */
    template <typename T>
    inline void delete_index(T index) {
        std::visit([&](auto& view) { Ops::delete_index(&view, index); }, variant);
    }

    /* A proxy for a list that allows for efficient operations on slices within
    the list. */
    class SliceProxy {
    public:

        /* Implement LinkedList.__getitem__() for all variants (slice). */
        VariantList* extract() {
            // get strong reference to VariantList
            auto ref = strong_ref();
            if (ref == nullptr) {
                return nullptr;  // propagate
            }

            // dispatch to proxy
            return std::visit(
                [&](auto& view) -> VariantList* {
                    // generate proxy
                    auto proxy = view.slice(start, stop, step);
                    if (!proxy.has_value()) {
                        return nullptr;  // propagate
                    }

                    // extract slice
                    auto result = Slice::extract(proxy.value());
                    if (!result.has_value()) {
                        return nullptr;  // propagate
                    }

                    // wrap result in VariantList
                    return new VariantList(std::move(result.value()));
                },
                ref->variant
            );
        }

        /* Implement LinkedList.__setitem__() for all variants (slice). */
        void replace(PyObject* items) {
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

                    // replace slice
                    Slice::replace(proxy.value(), items);
                },
                ref->variant
            );
        }

        /* Implement LinkedList.__delitem__() for all variants (slice). */
        void drop() {
            // get strong reference to VariantList
            auto ref = strong_ref();
            if (ref == nullptr) {
                return;  // propagate
            }

            // dispatch to proxy
            std::visit(
                [&](auto& view) {
                    // generate proxy
                    auto proxy = view.slice(start, stop, step);
                    if (!proxy.has_value()) {
                        return;  // propagate error
                    }

                    // drop slice
                    Slice::drop(proxy.value());
                },
                ref->variant
            );
        }

    private:
        std::weak_ptr<VariantList> variant;
        long long start;
        long long stop;
        long long step;

        // NOTE: the SliceProxy constructor is private to prevent users from
        // constructing SliceProxy objects directly.  Instead, they should always use
        // the `slice()` factory method on the VariantList itself.
        friend class VariantList;

        /* Construct a new SliceProxy from a VariantList and a slice. */
        SliceProxy(
            std::weak_ptr<VariantList> variant,
            long long start,
            long long stop,
            long long step
        ) : variant(variant), start(start), stop(stop), step(step)
        {}

        /* Get a strong reference to the original VariantList if it is still alive. */
        VariantList* strong_ref() {
            auto strong_ref = variant.lock();
            if (strong_ref == nullptr) {
                PyErr_SetString(
                    PyExc_ReferenceError,
                    "SliceProxy references a list that no longer exists"
                );
                return nullptr;  // propagate error
            }
            return strong_ref.get();
        }

    };

    /* Construct a SliceProxy for a list using the given indices. */
    inline SliceProxy slice(
        long long start,
        long long stop,
        long long step = 1
    ) {
        // lazily initialize self reference
        if (self == nullptr) {
            // NOTE: if we don't use a custom deleter, then the shared_ptr will
            // try to delete the VariantList when it goes out of scope.  This
            // causes a segfault due to a double-free.
            self = std::shared_ptr<VariantList>(this, [](VariantList*) {});
        }
        return SliceProxy(std::weak_ptr<VariantList>(self), start, stop, step);
    }

    /////////////////////////////
    ////    EXTRA METHODS    ////
    /////////////////////////////

    /* Get the specialization of the variant view. */
    inline PyObject* specialization() {
        return std::visit(
            [&](auto& view) {
                return Py_XNewRef(view.specialization);  // new ref may be NULL
            },
            variant
        );
    }

    /* Set the specialization of the variant view. */
    inline void specialize(PyObject* spec) {
        std::visit([&](auto& view) { view.specialize(spec); }, variant);
    }

    /* Lock the list for use in a multithreaded context. */
    inline std::lock_guard<std::mutex> lock() {
        return std::visit([&](auto& view) { return view.lock(); }, variant);
    }

    /* Lock the list for use in a multithreaded context. */
    inline std::lock_guard<std::mutex>* lock_context() {
        return std::visit([&](auto& view) { return view.lock_context(); }, variant);
    }

    /* Get the amount of memory being consumed by the view. */
    inline size_t nbytes() {
        return std::visit([&](auto& view) { return view.nbytes(); }, variant);
    }

    /* Check if the underlying view is doubly-linked. */
    inline bool doubly_linked() const {
        return _doubly_linked;
    }

    // NOTE: the following methods are used to implement the __iter__() and
    // __reversed__() methods in the Cython wrapper.  They are not intended to
    // be used from C++.

    /* Get the head node of a singly-linked list. */
    inline SingleNode* get_head_single() {
        return std::visit(
            [&](auto& view) -> SingleNode* {
                using View = std::decay_t<decltype(view)>;
                using Node = typename View::Node;

                // static_cast<> is only safe if the view is singly-linked
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
    std::shared_ptr<VariantList> self;  // allows weak references from proxies
};


#endif  // BERTRAND_STRUCTS_LIST_H include guard
