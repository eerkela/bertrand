// include guard: BERTRAND_STRUCTS_LINKED_LIST_CYTHON_H
#ifndef BERTRAND_STRUCTS_LINKED_LIST_CYTHON_H
#define BERTRAND_STRUCTS_LINKED_LIST_CYTHON_H

#include <cstddef>  // size_t
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

#include "base.h"  // SelfRef, VariantLock
#include "list.h"  // LinkedList


namespace bertrand {
namespace structs {
namespace linked {
namespace cython {


///////////////////////
////    PRIVATE    ////
///////////////////////


/* A std::variant encapsulating all the possible list types that are constructable
from Python. */
using ListVariant = std::variant<
    LinkedList<linked::SingleNode<PyObject*>, linked::MergeSort, util::BasicLock>,
    LinkedList<linked::DoubleNode<PyObject*>, linked::MergeSort, util::BasicLock>
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
class CyLinkedList {
private:
    using Self = SelfRef<CyLinkedList>;
    using WeakRef = Self::WeakRef;
    using SingleList = LinkedList<
        linked::SingleNode<PyObject*>, linked::MergeSort, util::BasicLock
    >;
    using DoubleList = LinkedList<
        linked::DoubleNode<PyObject*>, linked::MergeSort, util::BasicLock
    >;

    /* Select a variant based on constructor arguments. */
    template <typename... Args>
    inline static ListVariant select_variant(bool singly_linked, Args&&... args) {
        if (singly_linked) {
             return SingleList(std::forward<Args>(args)...);
        }
        return DoubleList(std::forward<Args>(args)...);
    }

public:
    using Lock = VariantLock<CyLinkedList>;

    class Index;
    template <typename... Args>
    class Slice;

    ListVariant variant;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Implement LinkedList.__init__() for cases where an input iterable is given. */
    CyLinkedList(
        PyObject* iterable,
        std::optional<size_t> max_size,
        PyObject* spec,
        bool reverse,
        bool singly_linked
    ) : variant(select_variant(singly_linked, iterable, max_size, spec, reverse)),
        lock(*this), weak_ref(*this)
    {}

    /* Implement LinkedList.__init__() for cases where no iterable is given. */
    CyLinkedList(std::optional<size_t> max_size, PyObject* spec, bool singly_linked) :
        variant(select_variant(singly_linked, max_size, spec)),
        lock(*this), weak_ref(*this)
    {}

    /* Construct a new CyLinkedList from an existing C++ LinkedList. */
    template <typename List>
    explicit CyLinkedList(List&& list) :
        variant(std::move(list)), lock(*this), weak_ref(*this)
    {}

    /* Move constructor. */
    explicit CyLinkedList(CyLinkedList&& other) :
        variant(std::move(other.variant)), lock(*this), weak_ref(*this)
    {}

    /* Move assignment operator. */
    CyLinkedList& operator=(CyLinkedList&& other) {
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

    /* Check whether the list's allocator supports dynamic resizing. */
    inline bool dynamic() const noexcept {
        return std::visit([&](auto& list) { return list.dynamic(); }, variant);
    }

    /* Check whether the list's allocator is currently frozen for memory stability. */
    inline bool frozen() const noexcept {
        return std::visit([&](auto& list) { return list.frozen(); }, variant);
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
    inline void insert(long long index, PyObject* item) {
        std::visit([&](auto& list) { list.insert(index, item); }, variant);
    }

    /* Insert LinkedList.extend() for all variants. */
    inline void extend(PyObject* items, bool left) {
        std::visit([&](auto& list) { list.extend(items, left); }, variant);
    }

    /* Implement LinkedList.index() for all variants. */
    inline size_t index(
        PyObject* item,
        std::optional<long long> start,
        std::optional<long long> stop
    ) const {
        return std::visit(
            [&](auto& list) {
                return list.index(item, start, stop);
            },
            variant
        );
    }

    /* Implement LinkedList.count() for all variants. */
    inline size_t count(
        PyObject* item,
        std::optional<long long> start,
        std::optional<long long> stop
    ) const {
        return std::visit(
            [&](auto& list) {
                return list.count(item, start, stop);
            },
            variant
        );
    }

    /* Implement LinkedList.__contains__() for all variants. */
    inline bool contains(PyObject* item) const {
        return std::visit([&](auto& list) { return list.contains(item); }, variant);
    }

    /* Implement LinkedList.remove() for all variants. */
    inline void remove(PyObject* item) {
        std::visit([&](auto& list) { list.remove(item); }, variant);
    }

    /* Implement LinkedList.pop() for all variants. */
    inline PyObject* pop(long long index) {
        return std::visit([&](auto& list) { return list.pop(index); }, variant);
    }

    /* Implement LinkedList.clear() for all variants. */
    inline void clear() {
        std::visit([&](auto& list) { list.clear(); }, variant);
    }

    /* Implement LinkedList.copy() for all variants. */
    inline util::Slot<CyLinkedList> copy() const {
        return std::visit(
            [&](auto& list) {
                util::Slot<CyLinkedList> slot;
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
    inline Index operator[](long long index) {
        return Index(weak_ref(), index);
    }

    /* Implement LinkedList.__getitem__() for all variants (slice). */
    template <typename... Args>
    inline Slice<Args...> slice(Args... args) {
        return Slice(weak_ref(), std::forward<Args>(args)...);
    }

    /* A proxy that represents a value at a particular index within a CyLinkedList. */
    class Index {
    public:

        /* Implement LinkedList.__getitem__() for all variants. */
        PyObject* get() {
            CyLinkedList* parent = ref.get();
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
            CyLinkedList* parent = ref.get();
            if (parent == nullptr) {
                return;  // propagate
            }
            std::visit([&](auto& list) { list[index].set(value); }, parent->variant);
        }

        /* Implement LinkedList.__delitem__() for all variants (single index). */
        void del() {
            CyLinkedList* parent = ref.get();
            if (parent == nullptr) {
                return;  // propagate
            }
            std::visit([&](auto& list) { list[index].del(); }, parent->variant);
        }

    private:
        friend CyLinkedList;
        WeakRef ref;
        long long index;

        /* Create a deferred index proxy. */
        Index(WeakRef self, long long index) : ref(self), index(index) {}
    };

    /* A proxy that represents a slice within a CyLinkedList. */
    template <typename... Args>
    class Slice {
    public:

        /* Implement LinkedList.__getitem__() for all variants. */
        util::Slot<CyLinkedList> get() {
            return std::visit(
                [&](auto& list) {
                    util::Slot<CyLinkedList> slot;
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
        friend CyLinkedList;
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
    template <typename Container>
    inline util::Slot<CyLinkedList> concat(const Container& c) {
        return std::visit(
            [&](auto& list) {
                util::Slot<CyLinkedList> slot;
                slot.construct(list + c);
                return slot;
            },
            variant
        );
    }

    /* Allow concatenation using the + operator (symmetric). */
    template <typename Container>
    inline util::Slot<CyLinkedList> rconcat(const Container& c) {
        return std::visit(
            [&](auto& list) {
                util::Slot<CyLinkedList> slot;
                slot.construct(c + list);
                return slot;
            },
            variant
        );
    }

    /* Allow in-place concatenation using the += operator. */
    template <typename Container>
    inline void iconcat(const Container& c) {
        return std::visit([&](auto& list) { list += c; }, variant);
    }

    /* Allow repetition using the * operator. */
    template <typename T>
    inline util::Slot<CyLinkedList> repeat(T rhs) {
        return std::visit(
            [&](auto& list) {
                util::Slot<CyLinkedList> slot;
                slot.construct(CyLinkedList(list * rhs));
                return slot;
            },
            variant
        );
    }

    /* Allow in-place repetition using the *= operator. */
    template <typename T>
    inline void irepeat(T rhs) {
        std::visit([&](auto& list) { list *= rhs; }, variant);
    }

    /* Allow lexicographic < comparisons. */
    template <typename Container>
    inline bool lt(const Container& c) {
        return std::visit([&](auto& list) { return list < c; }, variant);
    }

    /* Allow lexicographic <= comparisons. */
    template <typename Container>
    inline bool le(const Container& c) {
        return std::visit([&](auto& list) { return list <= c; }, variant);
    }

    /* Allow lexicographic == comparisons. */
    template <typename Container>
    inline bool eq(const Container& c) {
        return std::visit([&](auto& list) { return list == c; }, variant);
    }

    /* Allow lexicographic != comparisons. */
    template <typename Container>
    inline bool ne(const Container& c) {
        return std::visit([&](auto& list) { return list != c; }, variant);
    }

    /* Allow lexicographic >= comparisons. */
    template <typename Container>
    inline bool ge(const Container& c) {
        return std::visit([&](auto& list) { return list >= c; }, variant);
    }

    /* Allow lexicographic > comparisons. */
    template <typename Container>
    inline bool gt(const Container& c) {
        return std::visit([&](auto& list) { return list > c; }, variant);
    }

protected:
    friend Self;
    friend Lock;

    const Self weak_ref;  // functor to generate weak references to the variant
};


}  // namespace cython
}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_LIST_CYTHON_H
