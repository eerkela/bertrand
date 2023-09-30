// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_LIST_H
#define BERTRAND_STRUCTS_LIST_H

#include <cstddef>  // size_t
#include <memory>  // std::shared_ptr, std::weak_ptr
#include <optional>  // std::optional
#include <stdexcept>  // std::runtime_error
#include <utility>  // std::pair
#include <variant>  // std::variant
#include <Python.h>  // CPython API

// Core
#include "core/allocate.h"  // Allocator policies
#include "core/node.h"  // Nodes
#include "core/view.h"  // Views

#include "list.h"  // LinkedList


// TODO: Figure out how to carry operator overloads up to the VariantList level and
// make them callable from Python.


// TODO: could implement a Slot<T> Cython helper that can facilitate stack allocation
// in Cython.


// - Provide a C++ `Slot<typename T>` object that allocates raw memory for the size of
//   the template argument (`alignas(T) char slot[sizeof(T)]`.  This object should be
//   trivially constructible.
// - Provide a method on that object that uses placement new to construct the
//   underlying object in the preallocated memory.
// - Stack allocate the slot on the Cython class using its limited support.
// - Call the constructor method within the Cython wrapper's `__cinit__()` method to
//   finalize construction.
// - Rely on the slot's destructor to clean up the templated type as soon as the Cython
//   wrapper is garbage collected.

// The slot would also need to offer an `item()` method that returns a reference to the
// stored object, so that we could access its interface from Python.


// -> Actually, this can all be done with a std::optional<T> and the `emplace()`/`reset()`
// methods for construction/destruction.  We would need to subclass it though to provide
// copy/move semantics using pointers from Cython.

// The from_variant() method already matches Python's built-in list behavior with
// respect to subclasses.  We'd still need to assign it though, which would require a
// way to move values into the optional from Cython.


///////////////////////
////    PRIVATE    ////
///////////////////////


/* A std::variant encapsulating all the possible list types that are constructable
from Python. */
using ListAlternative = std::variant<
    LinkedList<SingleNode<PyObject*>, MergeSort, BasicLock>,
    // LinkedList<SingleNode, MergeSort, DiagnosticLock>,
    LinkedList<DoubleNode<PyObject*>, MergeSort, BasicLock>
    // LinkedList<DoubleNode, MergeSort, DiagnosticLock>,
>;


////////////////////////
////    FUNCTORS    ////
////////////////////////


/* A functor that generates weak references for the templated object. */
template <typename T>
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
        T* get() const {
            if (ref.expired()) {
                throw std::runtime_error("referenced object no longer exists");
            }
            return ref.lock().get();
        }

    private:
        friend SelfRef;
        std::weak_ptr<T> ref;

        template <typename... Args>
        WeakRef(Args... args) : ref(std::forward<Args>(args)...) {}
    };

    /* Get a weak reference to the associated object. */
    WeakRef operator()() const {
        return WeakRef(_self);
    }

private:
    friend T;
    const std::shared_ptr<T> _self;

    // NOTE: custom deleter prevents the shared_ptr from trying to delete the object
    // when it goes out of scope, which can cause a segfault due to a double free.

    SelfRef(T& self) : _self(&self, [](auto&) {}) {}
};


// TODO: Lock should return Python context managers a la PyIterator.  That way we
// don't need to worry about type erasure, and we can stack-allocate the locks within
// the Python type.  These are templated on the underlying lock type, but are opaque
// to Python.

// This would also eliminate the need for the unsafe `context()` methods on the lock
// functor, since the context manager can just wrap a lock object directly, with no
// extra pointer indirections or heap allocations.


/* A functor that allows the list to be locked for use in a multithreaded
environment. */
template <typename T>
class VariantLock {
public:
    using Guard = std::lock_guard<std::recursive_mutex>;  // TODO: figure out how to deal with different lock types

    /* Return an RAII-style lock guard for the underlying mutex. */
    inline Guard operator()() const {
        return std::visit(
            [&](auto& obj) {
                return obj.lock();
            }, 
            ref.variant
        );
    }

    /* Return a heap-allocated lock guard for the underlying mutex. */
    inline Guard* context() const {
        return std::visit(
            [&](auto& obj) {
                return obj.lock.context();
            },
            ref.variant
        );
    }

    /* Get the total number of times the mutex has been locked. */
    inline size_t count() const {
        return std::visit(
            [&](auto& obj) {
                return obj.lock.count();
            },
            ref.variant
        );
    }

    /* Get the total length of time spent waiting to acquire the lock. */
    inline size_t duration() const {
        return std::visit(
            [&](auto& obj) {
                return obj.lock.duration();
            },
            ref.variant
        );
    }

    /* Get the average time spent waiting to acquire the lock. */
    inline double contention() const {
        return std::visit(
            [&](auto& obj) {
                return obj.lock.average();
            },
            ref.variant
        );
    }

    /* Reset the internal diagnostic counters. */
    inline void reset_diagnostics() const {
        std::visit(
            [&](auto& obj) {
                obj.lock.reset_diagnostics();
            },
            ref.variant
        );
    }

private:
    friend T;
    T& ref;

    VariantLock(T& variant) : ref(variant) {}
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
private:
    using Self = SelfRef<VariantList>;
    using WeakRef = Self::WeakRef;
    using SingleList = LinkedList<SingleNode<PyObject*>, MergeSort, BasicLock>;
    using DoubleList = LinkedList<DoubleNode<PyObject*>, MergeSort, BasicLock>;

    // TODO: somehow these lists are being destroyed twice, which causes a segfault

    /* Select a variant based on constructor arguments. */
    template <typename... Args>
    inline static ListAlternative select_variant(bool doubly_linked, Args... args) {
        if (doubly_linked) {
            return DoubleList(std::forward<Args>(args)...); 
        }
        return SingleList(std::forward<Args>(args)...);
    }

public:
    using Lock = VariantLock<VariantList>;

    template <typename T>
    class Index;
    template <typename... Args>
    class Slice;

    ListAlternative variant;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Implement LinkedList.__init__() for cases where an input iterable is given. */
    VariantList(
        PyObject* iterable,
        bool doubly_linked,
        bool reverse,
        std::optional<size_t> max_size,
        PyObject* spec
    ) : variant(select_variant(doubly_linked, iterable, reverse, max_size, spec)),
        lock(*this), weak_ref(*this)
    {}

    /* Implement LinkedList.__init__() for cases where no iterable is given. */
    VariantList(bool doubly_linked, std::optional<size_t> max_size, PyObject* spec) :
        variant(select_variant(doubly_linked, max_size, spec)),
        lock(*this), weak_ref(*this)
    {}

    /* Construct a new VariantList from an existing C++ view. */
    template <typename View>
    VariantList(View&& view) :
        variant(std::move(view)), lock(*this), weak_ref(*this)
    {}

    /* Move constructor. */
    VariantList(VariantList&& other) :
        variant(std::move(other.variant)), lock(*this), weak_ref(*this)
    {}

    /* Move assignment operator. */
    VariantList& operator=(VariantList&& other) {
        variant = std::move(other.variant);
        return *this;
    }

    //////////////////////////////
    ////    LIST INTERFACE    ////
    //////////////////////////////

    /* Implement LinkedList.append() for all variants. */
    inline void append(PyObject* item, bool left) {
        std::visit([&](auto& list) { list.append(item, left); }, variant);
    }

    /* Implement LinkedList.insert() for all variants. */
    template <typename T>
    inline void insert(T index, PyObject* item) {
        std::visit([&](auto& list) { list.insert(index, item); }, variant);
    }

    /* Insert LinkedList.extend() for all variants. */
    inline void extend(PyObject* items, bool left) {
        std::visit([&](auto& list) { list.extend(items, left); }, variant);
    }

    /* Implement LinkedList.index() for all variants. */
    template <typename T>
    inline size_t index(PyObject* item, T start, T stop) {
        return std::visit(
            [&](auto& list) {
                return list.index(item, start, stop);
            },
            variant
        );
    }

    /* Implement LinkedList.count() for all variants. */
    template <typename T>
    inline size_t count(PyObject* item, T start, T stop) {
        return std::visit(
            [&](auto& list) {
                return list.count(item, start, stop);
            },
            variant
        );
    }

    /* Implement LinkedList.__contains__() for all variants. */
    inline bool contains(PyObject* item) {
        return std::visit([&](auto& list) { return list.contains(item); }, variant);
    }

    /* Implement LinkedList.remove() for all variants. */
    inline void remove(PyObject* item) {
        std::visit([&](auto& list) { list.remove(item); }, variant);
    }

    /* Implement LinkedList.pop() for all variants. */
    template <typename T>
    inline PyObject* pop(T index) {
        return std::visit([&](auto& list) { return list.pop(index); }, variant);
    }

    /* Implement LinkedList.clear() for all variants. */
    inline void clear() {
        std::visit([&](auto& list) { list.clear(); }, variant);
    }

    /* Implement LinkedList.copy() for all variants. */
    inline VariantList copy() {
        return std::visit(
            [&](auto& list) {
                return VariantList(std::move(list.copy()));
            },
            variant
        );
    }

    /* Implement LinkedList.sort() for all variants. */
    inline void sort(PyObject* key, bool reverse) {
        std::visit([&](auto& list) { list.sort(key, reverse); }, variant);
    }

    /* Implement LinkedList.reverse() for all variants. */
    inline void reverse() {
        std::visit([&](auto& list) { list.reverse(); }, variant);
    }

    /* Implement LinkedList.rotate() for all variants. */
    inline void rotate(long long steps) {
        std::visit([&](auto& list) { list.rotate(steps); }, variant);
    }

    /* Implement LinkedList.__len__() for all variants. */
    inline size_t size() {
        return std::visit([&](auto& list) { return list.size(); }, variant);
    }

    /* Implement LinkedList.__getitem__() for all variants (single index). */
    template <typename T>
    inline Index<T> operator[](T index) {
        return Index(weak_ref(), index);
    }

    /* Implement LinkedList.__getitem__() for all variants (slice). */
    template <typename... Args>
    inline Slice<Args...> slice(Args... args) {
        return Slice(weak_ref(), std::forward<Args>(args)...);
    }

    /* A proxy that represents a value at a particular index within a VariantList. */
    template <typename T>
    class Index {
    public:

        /* Implement LinkedList.__getitem__() for all variants. */
        PyObject* get() {
            VariantList* parent = ref.get();
            if (parent == nullptr) {
                return nullptr;  // propagate
            }
            return std::visit(
                [&](auto& list) -> PyObject* {
                    return list[index].get();
                },
                parent->variant
            );
        }

        /* Implement LinkedList.__setitem__() for all variants (single index). */
        void set(PyObject* value) {
            VariantList* parent = ref.get();
            if (parent == nullptr) {
                return;  // propagate
            }
            std::visit([&](auto& list) { list[index].set(value); }, parent->variant);
        }

        /* Implement LinkedList.__delitem__() for all variants (single index). */
        void del() {
            VariantList* parent = ref.get();
            if (parent == nullptr) {
                return;  // propagate
            }
            std::visit([&](auto& list) { list[index].del(); }, parent->variant);
        }

    private:
        friend VariantList;
        WeakRef ref;
        T index;

        /* Create a deferred index proxy. */
        Index(WeakRef self, T index) : ref(self), index(index) {}
    };

    /* A proxy that represents a slice within a VariantList. */
    template <typename... Args>
    class Slice {
    public:

        /* Implement LinkedList.__getitem__() for all variants. */
        VariantList get() {
            return std::visit(
                [&](auto& list) {
                    return VariantList(
                        std::apply(
                            [&](Args... args) {
                                return list.slice(std::forward<Args>(args)...).get();
                            },
                            args
                        )
                    );
                },
                ref.get()->variant
            );
        }

        /* Implement LinkedList.__setitem__() for all variants (slice). */
        void set(PyObject* items) {
            std::visit(
                [&](auto& list) {
                    // construct proxy using deferred arguments
                    auto proxy = std::apply(
                        [&](Args... args) { return list.slice(args...); }, args
                    );
 
                    // replace slice
                    proxy.set(items);
                },
                ref.get()->variant
            );
        }

        /* Implement LinkedList.__delitem__() for all variants (slice). */
        void del() {
            std::visit(
                [&](auto& list) {
                    // generate proxy
                    auto proxy = std::apply(
                        [&](Args... args) { return list.slice(args...); }, args
                    );

                    // drop slice
                    proxy.del();
                },
                ref.get()->variant
            );
        }

    private:
        friend VariantList;
        WeakRef ref;
        std::tuple<Args...> args;  // deferred arguments to list.slice()

        /* Create a deferred slice proxy. */
        Slice(WeakRef self, Args... args) :
            ref(self), args(std::make_tuple(std::forward<Args>(args)...))
        {}
    };

    //////////////////////////////////
    ////    OPERATOR OVERLOADS    ////
    //////////////////////////////////

    // template <typename T>
    // inline VariantList* concat(VariantList* lhs, T rhs) {
    //     return std::visit(
    //         [&](auto& list) {
    //             return new VariantList(list + rhs);
    //         },
    //         lhs->variant
    //     );
    // }

    // template <typename T>
    // inline VariantList* concat(T lhs, VariantList* rhs) {
    //     return std::visit(
    //         [&](auto& list) {
    //             return new VariantList(lhs + list);
    //         },
    //         rhs->variant
    //     );
    // }

    /////////////////////////////
    ////    EXTRA METHODS    ////
    /////////////////////////////

    const Lock lock;  // lock(), lock.context(), etc.

    /* Implement LinkedList.specialization() for all variants. */
    inline PyObject* specialization() {
        return std::visit(
            [&](auto& list) {
                return Py_XNewRef(list.specialization());  // ref may be NULL
            },
            variant
        );
    }

    /* Implement LinkedList.specialize() for all variants. */
    inline void specialize(PyObject* spec) {
        std::visit([&](auto& list) { list.specialize(spec); }, variant);
    }

    /* Implement LinkedList.nbytes() for all variants. */
    inline size_t nbytes() {
        return std::visit([&](auto& list) { return list.nbytes(); }, variant);
    }

    /* Implement LinkedList.__iter__() for all variants. */
    inline PyObject* iter() {
        return std::visit([&](auto& list) { return list.iter.python(); }, variant);
    }

    /* Implement LinkedList.__reversed__() for all variants. */
    inline PyObject* riter() {
        return std::visit([&](auto& list) { return list.iter.rpython(); }, variant);
    }

protected:
    friend Self;
    friend Lock;

    const Self weak_ref;  // functor to generate weak references to the variant
};


#endif  // BERTRAND_STRUCTS_LIST_H include guard
