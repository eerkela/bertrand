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
    ListView<DoubleNode, FixedAllocator>
    // ListView<Hashed<SingleNode>, DynamicAllocator>,
    // ListView<Hashed<SingleNode>, FixedAllocator>,
    // ListView<Hashed<DoubleNode>, DynamicAllocator>,
    // ListView<Hashed<DoubleNode>, FixedAllocator>,
    // SetView<SingleNode, DynamicAllocator>,
    // SetView<SingleNode, FixedAllocator>,
    // SetView<DoubleNode, DynamicAllocator>,
    // SetView<DoubleNode, FixedAllocator>
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
class VariantList : public WeakReferenceable<VariantList> {
public:
    using RefManager = WeakReferenceable<VariantList>;
    using WeakRef = RefManager::WeakRef;

    template <typename... Args>
    class Slice;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Implement LinkedList.__init__() for cases where an input iterable is given. */
    VariantList(
        PyObject* iterable,
        bool doubly_linked,
        bool reverse,
        Py_ssize_t max_size,
        PyObject* spec
    ) : RefManager()
    {
        if (doubly_linked) {
            if (max_size < 0) {
                view = ListView<DoubleNode, DynamicAllocator>(
                    iterable, reverse, max_size, spec
                );
            } else {
                view = ListView<DoubleNode, FixedAllocator>(
                    iterable, reverse, max_size, spec
                );
            }
        } else {
            if (max_size < 0) {
                view = ListView<SingleNode, DynamicAllocator>(
                    iterable, reverse, max_size, spec
                );
            } else {
                view = ListView<SingleNode, FixedAllocator>(
                    iterable, reverse, max_size, spec
                );
            }
        }
    }

    /* Implement LinkedList.__init__() for cases where no iterable is given. */
    VariantList(bool doubly_linked, Py_ssize_t max_size, PyObject* spec) : RefManager()
    {
        if (doubly_linked) {
            if (max_size < 0) {
                view = ListView<DoubleNode, DynamicAllocator>(max_size, spec);
            } else {
                view = ListView<DoubleNode, FixedAllocator>(max_size, spec);
            }
        } else {
            if (max_size < 0) {
                view = ListView<SingleNode, DynamicAllocator>(max_size, spec);
            } else {
                view = ListView<SingleNode, FixedAllocator>(max_size, spec);
            }
        }
    }

    /* Construct a new VariantList from an existing C++ view. */
    template <typename View>
    VariantList(View&& view) : RefManager(), view(std::move(view)) {}

    /* Move constructor. */
    VariantList(VariantList&& other) : RefManager(), view(std::move(other.view))
    {}

    /* Move assignment operator. */
    VariantList& operator=(VariantList&& other) {
        view = std::move(other.view);
        return *this;
    }

    //////////////////////////////
    ////    LIST INTERFACE    ////
    //////////////////////////////

    /* Implement LinkedList.append() for all views. */
    inline void append(PyObject* item, bool left) {
        std::visit([&](auto& view) { Ops::append(view, item, left); }, view);
    }

    /* Implement LinkedList.insert() for all views. */
    template <typename T>
    inline void insert(T index, PyObject* item) {
        std::visit([&](auto& view) { Ops::insert(view, index, item); }, view);
    }

    /* Insert LinkedList.extend() for all views. */
    inline void extend(PyObject* items, bool left) {
        std::visit([&](auto& view) { Ops::extend(view, items, left); }, view);
    }

    /* Implement LinkedList.index() for all views. */
    template <typename T>
    inline size_t index(PyObject* item, T start, T stop) {
        return std::visit(
            [&](auto& view) {
                return Ops::index(view, item, start, stop);
            },
            view
        );
    }

    /* Implement LinkedList.count() for all views. */
    template <typename T>
    inline size_t count(PyObject* item, T start, T stop) {
        return std::visit(
            [&](auto& view) {
                return Ops::count(view, item, start, stop);
            },
            view
        );
    }

    /* Implement LinkedList.remove() for all views. */
    inline void remove(PyObject* item) {
        std::visit([&](auto& view) { Ops::remove(view, item); }, view);
    }

    /* Implement LinkedList.pop() for all views. */
    template <typename T>
    inline PyObject* pop(T index) {
        return std::visit([&](auto& view) { return Ops::pop(view, index); }, view);
    }

    /* Implement LinkedList.copy() for all views. */
    inline VariantList* copy() {
        return std::visit(
            [&](auto& view) -> VariantList* {
                // copy underlying view
                auto copied = view.copy();
                if (!copied.has_value()) {
                    return nullptr;  // propagate Python errors
                }

                // wrap result as VariantList
                return new VariantList(std::move(copied.value()));
            },
            view
        );
    }

    /* Implement LinkedList.clear() for all views. */
    inline void clear() {
        std::visit([&](auto& view) { view.clear(); }, view);
    }

    /* Implement LinkedList.sort() for all views. */
    inline void sort(PyObject* key, bool reverse) {
        std::visit([&](auto& view) { Ops::sort(view, key, reverse); }, view);
    }

    /* Implement LinkedList.reverse() for all views. */
    inline void reverse() {
        std::visit([&](auto& view) { Ops::reverse(view); }, view);
    }

    /* Implement LinkedList.rotate() for all views. */
    inline void rotate(Py_ssize_t steps) {
        std::visit([&](auto& view) { Ops::rotate(view, steps); }, view);
    }

    /* Implement LinkedList.__len__() for all views. */
    inline size_t size() {
        return std::visit([&](auto& view) { return view.size; }, view);
    }

    /* Implement LinkedList.__contains__() for all views. */
    inline int contains(PyObject* item) {
        return std::visit([&](auto& view) { return Ops::contains(view, item); }, view);
    }

    /* Implement LinkedList.__getitem__() for all views (single index). */
    template <typename T>
    inline PyObject* get_index(T index) {
        return std::visit(
            [&](auto& view) {
                return Ops::get_index(view, index);
            },
            view
        );
    }

    /* Implement LinkedList.__setitem__() for all views (single index). */
    template <typename T>
    inline void set_index(T index, PyObject* value) {
        std::visit([&](auto& view) { Ops::set_index(view, index, value); }, view);
    }

    // TODO: rewrite set_slice using new Slice proxy

    /* Dispatch to the correct implementation of set_slice() for each views. */
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
            view
        );
    }

    /* Implement LinkedList.__delitem__() for all views (single index). */
    template <typename T>
    inline void delete_index(T index) {
        std::visit([&](auto& view) { Ops::delete_index(view, index); }, view);
    }

    /* Construct a deferred Slice proxy for a list. */
    template <typename... Args>
    inline Slice<Args...> slice(Args... args) {
        return Slice<Args...>(self(), args...);
    }

    /* A proxy for a list that allows for efficient operations on slices within
    the list. */
    template <typename... Args>
    class Slice {
    public:

        /* Implement LinkedList.__getitem__() for all views (slice). */
        VariantList* get() {
            VariantList* variant = ref.get();
            if (variant == nullptr) {
                return nullptr;  // propagate
            }

            // dispatch to proxy
            return std::visit(
                [&](auto& view) -> VariantList* {
                    // generate proxy
                    auto proxy = std::apply(
                        [&](Args... args) { return view.slice(args...); },
                        args
                    );
                    if (!proxy.has_value()) {
                        return nullptr;  // propagate
                    }

                    // extract slice
                    auto result = SliceOps::get(proxy.value());
                    if (!result.has_value()) {
                        return nullptr;  // propagate
                    }

                    // wrap result in VariantList
                    return new VariantList(std::move(result.value()));
                },
                variant->view
            );
        }

        /* Implement LinkedList.__setitem__() for all views (slice). */
        void set(PyObject* items) {
            VariantList* variant = ref.get();
            if (variant == nullptr) {
                return;  // propagate
            }

            // dispatch to proxy
            std::visit(
                [&](auto& view) {
                    auto proxy = std::apply(
                        [&](Args... args) { return view.slice(args...); },
                        args
                    );
                    if (!proxy.has_value()) {
                        return;  // propagate error
                    }

                    // replace slice
                    SliceOps::set(proxy.value(), items);
                },
                variant->view
            );
        }

        /* Implement LinkedList.__delitem__() for all views (slice). */
        void del() {
            VariantList* variant = ref.get();
            if (variant == nullptr) {
                return;  // propagate
            }

            // dispatch to proxy
            std::visit(
                [&](auto& view) {
                    // generate proxy
                    auto proxy = std::apply(
                        [&](Args... args) { return view.slice(args...); },
                        args
                    );
                    if (!proxy.has_value()) {
                        return;  // propagate error
                    }

                    // drop slice
                    SliceOps::del(proxy.value());
                },
                variant->view
            );
        }

    private:
        friend class VariantList;
        WeakRef ref;
        std::tuple<Args...> args;  // deferred arguments to view.slice()

        /* Create a deferred factory for View::Slice objects. */
        Slice(WeakRef variant, Args... args) :
            ref(variant), args(std::make_tuple(args...))
        {}

    };

    /////////////////////////////
    ////    EXTRA METHODS    ////
    /////////////////////////////

    /* Implement LinkedList.specialization() for all views. */
    inline PyObject* specialization() {
        return std::visit(
            [&](auto& view) {
                return Py_XNewRef(view.specialization);  // new ref may be NULL
            },
            view
        );
    }

    /* Implement LinkedList.specialize() for all views. */
    inline void specialize(PyObject* spec) {
        std::visit([&](auto& view) { view.specialize(spec); }, view);
    }

    /* (C++ only) Lock the list for use in a multithreaded context (RAII-style). */
    inline std::lock_guard<std::mutex> lock() {
        return std::visit([&](auto& view) { return view.lock(); }, view);
    }

    /* Implement LinkedList.lock() for all views. */
    inline std::lock_guard<std::mutex>* lock_context() {
        return std::visit([&](auto& view) { return view.lock_context(); }, view);
    }

    /* Implement LinkedList.nbytes() for all views. */
    inline size_t nbytes() {
        return std::visit([&](auto& view) { return view.nbytes(); }, view);
    }

    // NOTE: the following methods are used to implement the __iter__() and
    // __reversed__() methods in the Cython wrapper.  They are not intended to
    // be used from C++.

    /* Check if the underlying view is doubly-linked. */
    inline bool doubly_linked() const {
        return std::visit(
            [&](auto& view) -> bool {
                using View = std::decay_t<decltype(view)>;
                return has_prev<typename View::Node>::value;
            },
            view
        );
    }

    /* Get the head node of a singly-linked list. */
    inline SingleNode* get_head_single() {
        return std::visit(
            [&](auto& view) -> SingleNode* {
                using View = std::decay_t<decltype(view)>;

                // static_cast<> is only safe if the view is singly-linked
                if constexpr (has_prev<typename View::Node>::value) {
                    throw std::runtime_error("List is not singly-linked.");
                } else {
                    return static_cast<SingleNode*>(view.head);
                }
            },
            view
        );
    }

    /* Get the tail node of a singly-linked list. */
    inline SingleNode* get_tail_single() {
        return std::visit(
            [&](auto& view) -> SingleNode* {
                using View = std::decay_t<decltype(view)>;

                if constexpr (has_prev<typename View::Node>::value) {
                    throw std::runtime_error("List is not singly-linked.");
                } else {
                    return static_cast<SingleNode*>(view.tail);
                }
            },
            view
        );
    }

    /* Get the head node of a doubly-linked list. */
    inline DoubleNode* get_head_double() {
        return std::visit(
            [&](auto& view) -> DoubleNode* {
                using View = std::decay_t<decltype(view)>;

                if constexpr (has_prev<typename View::Node>::value) {
                    return static_cast<DoubleNode*>(view.head);
                } else {
                    throw std::runtime_error("List is not doubly-linked.");
                }
            },
            view
        );
    }

    /* Get the tail node of a doubly-linked list. */
    inline DoubleNode* get_tail_double() {
        return std::visit(
            [&](auto& view) -> DoubleNode* {
                using View = std::decay_t<decltype(view)>;

                if constexpr (has_prev<typename View::Node>::value) {
                    return static_cast<DoubleNode*>(view.tail);
                } else {
                    throw std::runtime_error("List is not doubly-linked.");
                }
            },
            view
        );
    }

protected:
    VariantView view;

};



#endif  // BERTRAND_STRUCTS_LIST_H include guard
