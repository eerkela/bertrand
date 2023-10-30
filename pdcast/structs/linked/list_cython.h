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
#include "../util/slot.h"  // Slot

// Core
#include "core/allocate.h"  // Allocator policies
#include "core/node.h"  // Nodes
#include "core/view.h"  // Views

#include "list.h"  // LinkedList


// TODO: Figure out how to carry operator overloads up to the VariantList level and
// make them callable from Python.


/* Namespaces reflect file system and Python import path. */
namespace bertrand {
namespace structs {
namespace cython {


///////////////////////
////    PRIVATE    ////
///////////////////////


/* A std::variant encapsulating all the possible list types that are constructable
from Python. */
using ListAlternative = std::variant<
    LinkedList<linked::SingleNode<PyObject*>, IList::MergeSort, util::BasicLock>,
    // LinkedList<SingleNode, IList::MergeSort, DiagnosticLock>,
    LinkedList<linked::DoubleNode<PyObject*>, IList::MergeSort, util::BasicLock>
    // LinkedList<DoubleNode, IList::MergeSort, DiagnosticLock>,
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


/* A functor that allows the list to be locked for use in a multithreaded
environment. */
template <typename T>
class VariantLock {
public:

    /* Return a Python context manager containing an exclusive lock on the mutex. */
    inline PyObject* operator()() const {
        return std::visit(
            [&](auto& obj) {
                return obj.lock.python();
            }, 
            ref.variant
        );
    }

    /* Return a Python context manager containing a shared lock on the mutex. */
    inline PyObject* shared() const {
        return std::visit(
            [&](auto& obj) {
                return obj.lock.shared_python();
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
    using SingleList = LinkedList<
        linked::SingleNode<PyObject*>, IList::MergeSort, util::BasicLock
    >;
    using DoubleList = LinkedList<
        linked::DoubleNode<PyObject*>, IList::MergeSort, util::BasicLock
    >;

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

    /* Construct a new VariantList from an existing C++ LinkedList. */
    template <typename List>
    explicit VariantList(List&& list) :
        variant(std::move(list)), lock(*this), weak_ref(*this)
    {}

    /* Move constructor. */
    explicit VariantList(VariantList&& other) :
        variant(std::move(other.variant)), lock(*this), weak_ref(*this)
    {}

    /* Move assignment operator. */
    VariantList& operator=(VariantList&& other) {
        variant = std::move(other.variant);
        return *this;
    }

    /////////////////////////////////
    ////    LOW-LEVEL METHODS    ////
    /////////////////////////////////

    /* Check whether the list contains any elements. */
    inline bool empty() const noexcept {
        return std::visit([&](auto& list) { return list.empty(); }, variant);
    }

    /* Get the number of elements within the list. */
    inline size_t size() const noexcept {
        return std::visit([&](auto& list) { return list.size(); }, variant);
    }

    /* Get the current length of the allocator's dynamic array. */
    inline size_t capacity() const noexcept {
        return std::visit([&](auto& list) { return list.capacity(); }, variant);
    }

    /* Get the list's current size limit. */
    inline std::optional<size_t> max_size() const noexcept {
        return std::visit([&](auto& list) { return list.max_size(); }, variant);
    }

    /* Reserve space for a list of a given size. */
    inline void reserve(size_t capacity) {
        std::visit([&](auto& list) { list.reserve(capacity); }, variant);
    }

    /* Rearrange the allocator's contents to match the current list order. */
    inline void defragment() {
        std::visit([&](auto& list) { list.defragment(); }, variant);
    }

    /* Get the current Python specialization for the list's elements. */
    inline PyObject* specialization() const noexcept {
        return std::visit(
            [&](auto& list) {
                return Py_XNewRef(list.specialization());  // ref may be NULL
            },
            variant
        );
    }

    /* Enforce strict typing for Python objects within a list. */
    inline void specialize(PyObject* spec) {
        std::visit([&](auto& list) { list.specialize(spec); }, variant);
    }

    /* Get the total memory consumed by a list. */
    inline size_t nbytes() {
        return std::visit([&](auto& list) { return list.nbytes(); }, variant);
    }

    /* Return a forward iterator over the list. */
    inline PyObject* iter() {
        return std::visit(
            [&](auto& list) {
                return util::iter(list).python();
            },
            variant
        );
    }

    /* Return a reverse iterator over the list. */
    inline PyObject* riter() {
        return std::visit(
            [&](auto& list) {
                return util::iter(list).rpython();
            },
            variant
        );
    }

    /* Forward the list's lock() functor. */
    const Lock lock;  // lock(), lock.shared(), etc.

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
    inline util::Slot<VariantList> copy() {
        return std::visit(
            [&](auto& list) {
                util::Slot<VariantList> slot;
                slot.construct(std::move(list.copy()));
                return slot;
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
        util::Slot<VariantList> get() {
            return std::visit(
                [&](auto& list) {
                    util::Slot<VariantList> slot;
                    slot.construct(
                        std::apply(
                            [&](Args... args) {
                                return list.slice(std::forward<Args>(args)...).get();
                            },
                            args
                        )
                    );
                    return slot;
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

    /* Allow concatenation using the + operator. */
    template <typename T>
    inline util::Slot<VariantList> concat(T rhs) {
        return std::visit(
            [&](auto& list) {
                util::Slot<VariantList> slot;
                slot.construct(list + rhs);
                return slot;
            },
            variant
        );
    }

    /* Allow concatenation using the + operator (symmetric). */
    template <typename T>
    inline util::Slot<T> concatenate(T lhs, VariantList& rhs) {
        return std::visit(
            [&](auto& list) {
                util::Slot<T> slot;
                slot.construct(lhs + list);
                return slot;
            },
            rhs.variant
        );
    }

    /* Allow repetition using the * operator. */
    template <typename T>
    inline util::Slot<VariantList> repeat(VariantList& lhs, T rhs) {
        return std::visit(
            [&](auto& list) {
                util::Slot<VariantList> slot;
                slot.construct(VariantList(list * rhs));
                return slot;
            },
            lhs.variant
        );
    }

    /* Allow repetition using the * operator (symmetric). */
    template <typename T>
    inline util::Slot<VariantList> repeat(T lhs, VariantList& rhs) {
        return std::visit(
            [&](auto& list) {
                util::Slot<VariantList> slot;
                slot.construct(VariantList(lhs * list));
                return slot;
            },
            rhs.variant
        );
    }

    /* Allow in-place repetition using the *= operator. */
    template <typename T>
    inline void irepeat(VariantList& lhs, T rhs) {
        std::visit([&](auto& list) { list *= rhs; }, lhs.variant);
    }

    /* Allow lexicographic < comparisons. */
    template <typename T>
    inline bool lt(T rhs) {
        return std::visit([&](auto& list) { return list < rhs; }, variant);
    }

    /* Allow lexicographic < comparisons (symmetric). */
    template <typename T>
    inline bool rlt(T lhs) {
        return std::visit([&](auto& list) { return lhs < list; }, variant);
    }

    /* Allow lexicographic <= comparisons. */
    template <typename T>
    inline bool lexical_le(VariantList& lhs, T rhs) {
        return std::visit([&](auto& list) { return list <= rhs; }, lhs.variant);
    }

    /* Allow lexicographic <= comparisons (symmetric). */
    template <typename T>
    inline bool lexical_le(T lhs, VariantList& rhs) {
        return std::visit([&](auto& list) { return lhs <= list; }, rhs.variant);
    }

    /* Allow lexicographic == comparisons. */
    template <typename T>
    inline bool lexical_eq(VariantList& lhs, T rhs) {
        return std::visit([&](auto& list) { return list == rhs; }, lhs.variant);
    }

    /* Allow lexicographic == comparisons (symmetric). */
    template <typename T>
    inline bool lexical_eq(T lhs, VariantList& rhs) {
        return std::visit([&](auto& list) { return lhs == list; }, rhs.variant);
    }

    /* Allow lexicographic != comparisons. */
    template <typename T>
    inline bool lexical_ne(VariantList& lhs, T rhs) {
        return std::visit([&](auto& list) { return list != rhs; }, lhs.variant);
    }

    /* Allow lexicographic != comparisons (symmetric). */
    template <typename T>
    inline bool lexical_ne(T lhs, VariantList& rhs) {
        return std::visit([&](auto& list) { return lhs != list; }, rhs.variant);
    }

    /* Allow lexicographic >= comparisons. */
    template <typename T>
    inline bool lexical_ge(VariantList& lhs, T rhs) {
        return std::visit([&](auto& list) { return list >= rhs; }, lhs.variant);
    }

    /* Allow lexicographic >= comparisons (symmetric). */
    template <typename T>
    inline bool lexical_ge(T lhs, VariantList& rhs) {
        return std::visit([&](auto& list) { return lhs >= list; }, rhs.variant);
    }

    /* Allow lexicographic > comparisons. */
    template <typename T>
    inline bool lexical_gt(VariantList& lhs, T rhs) {
        return std::visit([&](auto& list) { return list > rhs; }, lhs.variant);
    }

    /* Allow lexicographic > comparisons (symmetric). */
    template <typename T>
    inline bool lexical_gt(T lhs, VariantList& rhs) {
        return std::visit([&](auto& list) { return lhs > list; }, rhs.variant);
    }

protected:
    friend Self;
    friend Lock;

    const Self weak_ref;  // functor to generate weak references to the variant
};


//////////////////////////////////
////    OPERATOR OVERLOADS    ////
//////////////////////////////////


}  // namespace cython
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LIST_H include guard
