// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_LIST_H
#define BERTRAND_STRUCTS_LIST_H

#include <cstddef>  // for size_t
#include <memory>  // std::shared_ptr, std::weak_ptr
#include <optional>  // std::optional
#include <stdexcept>  // std::runtime_error
#include <utility>  // std::pair
#include <variant>  // std::variant
#include <Python.h>  // CPython API

// Algorithms
// #include "algorithms/append.h"
// #include "algorithms/contains.h"
// #include "algorithms/count.h"
#include "algorithms/delete.h"
// #include "algorithms/extend.h"
#include "algorithms/get.h"
// #include "algorithms/index.h"
// #include "algorithms/insert.h"
// #include "algorithms/pop.h"
// #include "algorithms/remove.h"
// #include "algorithms/reverse.h"
// #include "algorithms/rotate.h"
#include "algorithms/set.h"
#include "algorithms/sort.h"

// Core
#include "core/allocate.h"  // Allocator policies
#include "core/node.h"  // Nodes
#include "core/view.h"  // Views


#include "list.h"  // LinkedList


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
    // LinkedList<DoubleNode, DynamicAllocator, MergeSort, BasicLock>
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


LinkedList<DoubleNode, DynamicAllocator, MergeSort, BasicLock> list;
// std::vector<int> test = std::vector<int> {1, 2, 3} + list;


////////////////////////
////    FUNCTORS    ////
////////////////////////


/* A functor that generates weak references for the templated object. */
template <typename SelfType>
class SelfRef {
public:

    /* A weak reference to the associated object. */
    class WeakRef {
    public:

        /* Check whether the referenced object still exists. */
        bool exists() const {
            return !ref.expired();
        }

        /* Follow the weak reference, yielding a pointer to the referenced object if it
        still exists.  Otherwise, sets a Python error and return nullptr.  */
        SelfType* get() const {
            if (ref.expired()) {
                PyErr_SetString(
                    PyExc_ReferenceError,
                    "referenced object no longer exists"
                );
                return nullptr;  // propagate error
            }
            return ref.lock().get();
        }

    private:
        friend SelfRef;
        std::weak_ptr<SelfType> ref;

        template <typename... Args>
        WeakRef(Args... args) : ref(args...) {}
    };

    /* Get a weak reference to the associated object. */
    WeakRef operator()() const {
        return WeakRef(_self);
    }

private:
    friend SelfType;
    const std::shared_ptr<SelfType> _self;

    // NOTE: custom deleter prevents the shared_ptr from trying to delete the object
    // when it goes out of scope, which can cause a segfault due to a double free.

    SelfRef(SelfType& self) : _self(&self, [](auto&) {}) {}
};


/* A functor that allows the list to be locked for use in a multithreaded
environment. */
template <typename VariantType>
class VariantLock {
public:
    using Guard = std::lock_guard<std::mutex>;

    /* Return an RAII-style lock guard for the underlying mutex. */
    inline Guard operator()() const {
        return std::visit(
            [&](auto& view) {
                return view.lock();
            }, 
            variant.view
        );
    }

    /* Return a heap-allocated lock guard for the underlying mutex. */
    inline Guard* context() const {
        return std::visit(
            [&](auto& view) {
                return view.lock.context();
            },
            variant.view
        );
    }

    /* Get the total number of times the mutex has been locked. */
    inline size_t count() const {
        return std::visit(
            [&](auto& view) {
                return view.lock.count();
            },
            variant.view
        );
    }

    /* Get the total length of time spent waiting to acquire the lock. */
    inline size_t duration() const {
        return std::visit(
            [&](auto& view) {
                return view.lock.duration();
            },
            variant.view
        );
    }

    /* Get the average time spent waiting to acquire the lock. */
    inline double contention() const {
        return std::visit(
            [&](auto& view) {
                return view.lock.average();
            },
            variant.view
        );
    }

    /* Reset the internal diagnostic counters. */
    inline void reset_diagnostics() const {
        std::visit(
            [&](auto& view) {
                view.lock.reset_diagnostics();
            },
            variant.view
        );
    }

private:
    friend VariantType;
    VariantType& variant;
    VariantLock(VariantType& variant) : variant(variant) {}
};


/* A functor that generates slices for the list. */
template <typename VariantType>
class VariantSlice {
public:

    /* A simple container for normalized slice indices. */
    struct Indices {
        long long start, stop, step;
        size_t abs_step, first, last, length;
        bool empty, consistent, backward;
    };

    /* A proxy for a list that allows for efficient operations on slices within
    the list. */
    template <typename... Args>
    class Slice {
    public:

        /* Implement LinkedList.__getitem__() for all views (slice). */
        VariantType* get() {
            VariantType* variant = ref.get();
            if (variant == nullptr) {
                return nullptr;  // propagate
            }

            // dispatch to proxy
            return std::visit(
                [&](auto& view) -> VariantType* {
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

                    // wrap result in VariantType
                    return new VariantType(std::move(result.value()));
                },
                variant->view
            );
        }

        /* Implement LinkedList.__setitem__() for all views (slice). */
        void set(PyObject* items) {
            VariantType* variant = ref.get();
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
            VariantType* variant = ref.get();
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
        friend VariantType;
        friend VariantSlice;
        using WeakRef = typename VariantType::WeakRef;
        WeakRef ref;
        std::tuple<Args...> args;  // deferred arguments to view.slice()

        /* Create a deferred factory for View::Slice objects. */
        Slice(WeakRef variant, Args... args) :
            ref(variant), args(std::make_tuple(args...))
        {}

    };

    /* Construct a deferred Slice proxy for a list. */
    template <typename... Args>
    inline Slice<Args...> operator()(Args... args) const {
        return Slice<Args...>(variant.weak_ref(), args...);
    }

    /* Normalize slice indices, applying Python-style wraparound and bounds
    checking. */
    template <typename... Args>
    inline Indices* normalize(Args... args) const {
        return std::visit(
            [&](auto& view) -> Indices* {
                auto result = view.slice.normalize(args...);
                if (!result.has_value()) {
                    return nullptr;
                }
                auto indices = result.value();
                return new Indices {
                    indices.start(),
                    indices.stop(),
                    indices.step(),
                    indices.abs_step(),
                    indices.first(),
                    indices.last(),
                    indices.length(),
                    indices.empty(),
                    indices.consistent(),
                    indices.backward()
                };
            },
            variant.view
        );
    }

private:
    friend VariantType;
    VariantType& variant;
    VariantSlice(VariantType& variant) : variant(variant) {}
};


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
    using Self = SelfRef<VariantList>;
    using WeakRef = Self::WeakRef;
    using Lock = VariantLock<VariantList>;
    using SliceFactory = VariantSlice<VariantList>;

    template <typename... Args>
    using Slice = SliceFactory::Slice<Args...>;

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
    ) : slice(*this), lock(*this), weak_ref(*this)
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
    VariantList(bool doubly_linked, Py_ssize_t max_size, PyObject* spec) :
        slice(*this), lock(*this), weak_ref(*this)
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
    VariantList(View&& view) :
        slice(*this), lock(*this), weak_ref(*this), view(std::move(view))
    {}

    /* Move constructor. */
    VariantList(VariantList&& other) :
        slice(*this), lock(*this), weak_ref(*this), view(std::move(other.view))
    {}

    /* Move assignment operator. */
    VariantList& operator=(VariantList&& other) {
        view = std::move(other.view);
        return *this;
    }

    //////////////////////////////
    ////    LIST INTERFACE    ////
    //////////////////////////////

    const SliceFactory slice;  // slice(), slice.normalize(), etc.

    /* Implement LinkedList.append() for all views. */
    inline void append(PyObject* item, bool left) {
        std::visit([&](auto& view) { view.append(item, left); }, view);
    }

    /* Implement LinkedList.insert() for all views. */
    template <typename T>
    inline void insert(T index, PyObject* item) {
        std::visit([&](auto& view) { view.insert(index, item); }, view);
    }

    /* Insert LinkedList.extend() for all views. */
    inline void extend(PyObject* items, bool left) {
        std::visit([&](auto& view) { view.extend(items, left); }, view);
    }

    /* Implement LinkedList.index() for all views. */
    template <typename T>
    inline size_t index(PyObject* item, T start, T stop) {
        return std::visit(
            [&](auto& view) {
                auto result = view.index(item, start, stop);
                if (!result.has_value()) {
                    return MAX_SIZE_T;  // Cython expects an explicit error code
                }
                return result.value();
            },
            view
        );
    }

    /* Implement LinkedList.count() for all views. */
    template <typename T>
    inline size_t count(PyObject* item, T start, T stop) {
        return std::visit(
            [&](auto& view) {
                auto result = view.count(item, start, stop);
                if (!result.has_value()) {
                    return MAX_SIZE_T;  // Cython expects an explicit error code
                }
                return result.value();
            },
            view
        );
    }

    /* Implement LinkedList.__contains__() for all views. */
    inline int contains(PyObject* item) {
        return std::visit(
            [&](auto& view) {
                auto result = view.contains(item);
                if (!result.has_value()) {
                    return -1;  // Cython expects an explicit error code
                }
                return static_cast<int>(result.value());
            },
            view
        );
    }

    /* Implement LinkedList.remove() for all views. */
    inline void remove(PyObject* item) {
        std::visit([&](auto& view) { view.remove(item); }, view);
    }

    /* Implement LinkedList.pop() for all views. */
    template <typename T>
    inline PyObject* pop(T index) {
        return std::visit([&](auto& view) { return view.pop(index); }, view);
    }

    /* Implement LinkedList.clear() for all views. */
    inline void clear() {
        std::visit([&](auto& view) { view.clear(); }, view);
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

    /* Implement LinkedList.sort() for all views. */
    inline void sort(PyObject* key, bool reverse) {
        std::visit([&](auto& view) { Ops::sort(view, key, reverse); }, view);
    }

    /* Implement LinkedList.reverse() for all views. */
    inline void reverse() {
        std::visit([&](auto& view) { view.reverse(); }, view);
    }

    /* Implement LinkedList.rotate() for all views. */
    inline void rotate(long long steps) {
        std::visit([&](auto& view) { view.rotate(steps); }, view);
    }

    /* Implement LinkedList.__len__() for all views. */
    inline size_t size() {
        return std::visit([&](auto& view) { return view.size; }, view);
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

    /* Implement LinkedList.__delitem__() for all views (single index). */
    template <typename T>
    inline void delete_index(T index) {
        std::visit([&](auto& view) { Ops::delete_index(view, index); }, view);
    }

    /////////////////////////////
    ////    EXTRA METHODS    ////
    /////////////////////////////

    const Lock lock;  // lock(), lock.context(), etc.

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
    friend Self;
    friend Lock;
    friend SliceFactory;

    const Self weak_ref;  // weak_ref()
    VariantView view;
};


#endif  // BERTRAND_STRUCTS_LIST_H include guard
