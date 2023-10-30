// include guard: BERTRAND_STRUCTS_LINKED_VIEW_H
#ifndef BERTRAND_STRUCTS_LINKED_VIEW_H
#define BERTRAND_STRUCTS_LINKED_VIEW_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <stdexcept>  // std::invalid_argument
#include <tuple>  // std::tuple
#include <Python.h>  // CPython API
#include "node.h"  // Hashed<>, Mapped<>
#include "allocate.h"  // Allocator
#include "iter.h"  // Iterator, Direction
#include "../../util/iter.h"  // iter()


namespace bertrand {
namespace structs {
namespace linked {


// TODO: BaseView should support a converting copy/move constructor that accepts
// another BaseView with any type of allocator.


// TODO: maybe sort() should just be generic, rather than casting to ListView.  That
// would completely eliminate the need for converting copies/moves.


////////////////////
////    BASE    ////
////////////////////


/* Base class representing the low-level core of a linked data structure.

Views are responsible for managing the memory of and links between the underlying
nodes, iteration over them, and references to the head/tail of the list as well as its
current size.  They are lightweight wrappers around a raw memory allocator that exposes
the following interface:

class Allocator {
public:
    Node* head;
    Node* tail;
    size_t capacity;  // total number of nodes in existence
    size_t occupied;  // equivalent to view.size()
    PyObject* specialization;
    bool frozen;  // indicates whether or not the data structure can grow dynamically

    Node* create(Args...);  // forward to Node constructor
    void recycle(Node* node);
    void reserve(size_t capacity);
    void defragment();
    void clear();
    void specialize(PyObject* spec);
    size_t nbytes();
};

To this, they add a number of convenience methods for high-level list manipulations,
which are then used in the various non-member method headers listed in the algorithms/
directory.  This setup allows for ultimate configurability and code reuse, since
methods can be mixed, matched, and specialized alongside views to produce a variety of
flexible and highly-optimized data structures, without worrying about low-level
implementation details.
*/
template <typename Derived, typename Allocator>
class BaseView {
public:
    using Node = typename Allocator::Node;
    using Value = typename Node::Value;

    template <Direction dir>
    using Iterator = linked::Iterator<BaseView, dir>;
    template <Direction dir>
    using ConstIterator = linked::Iterator<const BaseView, dir>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct an empty view. */
    BaseView(std::optional<size_t> max_size = std::nullopt, PyObject* spec = nullptr) :
        allocator(max_size, spec)
    {}

    /* Construct a view from an input iterable. */
    BaseView(
        PyObject* iterable,
        bool reverse = false,
        std::optional<size_t> max_size = std::nullopt,
        PyObject* spec = nullptr
    ) : allocator(max_size, spec)
    {
        using util::iter;
        for (auto item : iter(iterable)) {
            // NOTE: node() constructor is fallible, but because all memory is managed
            // by the encapsulated allocator, we don't need to worry about cleaning up
            // the list in the event of an exception.
            Node* curr = node(item);
            if (reverse) {
                link(nullptr, curr, head());
            } else {
                link(tail(), curr, nullptr);
            }
        }
    }

    /* Move constructor: transfer ownership from one view to another. */
    BaseView(BaseView&& other) noexcept : allocator(std::move(other.allocator)) {}

    /* Move assignment: transfer ownership from one view to another. */
    BaseView& operator=(BaseView&& other) noexcept {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // transfer ownership of nodes
        allocator = std::move(other.allocator);
        return *this;
    }

    /* Copy constructors. These are disabled for the sake of efficiency, preventing us
    from unintentionally copying data.  Use the explicit copy() method instead. */
    BaseView(const BaseView& other) = delete;
    BaseView& operator=(const BaseView&) = delete;

    ///////////////////////////////
    ////    NODE MANAGEMENT    ////
    ///////////////////////////////

    /* Get the head of the list. */
    inline Node* head() const noexcept {
        return allocator.head;
    }

    /* Set the head of the list to another node. */
    inline void head(Node* node) {
        if (node != nullptr && !allocator.owns(node)) {
            throw std::invalid_argument("node must be owned by this allocator");
        }
        allocator.head = node;
    }

    /* Get the tail of the list. */
    inline Node* tail() const noexcept {
        return allocator.tail;
    }
 
    /* Set the tail of the list to another node. */
    inline void tail(Node* node) {
        if (node != nullptr && !allocator.owns(node)) {
            throw std::invalid_argument("node must be owned by this allocator");
        }
        allocator.tail = node;
    }

    /* Construct a new node for the list. */
    template <typename... Args>
    inline Node* node(Args&&... args) const {
        return allocator.create(std::forward<Args>(args)...);
    }

    /* Release a node, returning it to the allocator. */
    inline void recycle(Node* node) const {
        allocator.recycle(node);
    }

    /* Make a shallow copy of the entire list. */
    inline Derived copy() const {
        using util::iter;

        Derived result(max_size(), specialization());
        for (auto it = iter(*this).forward(); it != it.end(); ++it) {
            Node* copied = result.node(*it.curr());
            result.link(result.tail(), copied, nullptr);
        }
        return result;
    }

    /* Remove all elements from a list. */
    inline void clear() noexcept {
        allocator.clear();
    }

    /* Link a node to its neighbors to form a linked list. */
    inline void link(Node* prev, Node* curr, Node* next) {
        Node::link(prev, curr, next);  // as defined by Node
        if (prev == nullptr) {
            head(curr);
        }
        if (next == nullptr) {
            tail(curr);
        }
    }

    /* Unlink a node from its neighbors. */
    inline void unlink(Node* prev, Node* curr, Node* next) {
        Node::unlink(prev, curr, next);  // as defined by Node
        if (prev == nullptr) {
            head(next);
        }
        if (next == nullptr) {
            tail(prev);
        }
    }

    /* Get the current size of the list. */
    inline size_t size() const noexcept {
        return allocator.occupied;
    }

    /* Get the current capacity of the allocator array. */
    inline size_t capacity() const noexcept {
        return this->allocator.capacity;
    }

    /* Get the maximum size of the list. */
    inline std::optional<size_t> max_size() const noexcept {
        return this->allocator.frozen ?
            std::make_optional(this->allocator.capacity) :
            std::nullopt;
    }

    /* Reserve memory for a given number of nodes ahead of time. */
    inline void reserve(size_t capacity) const {
        this->allocator.reserve(capacity);
    }

    /* Rearrange the nodes in memory to match their positions within the list. */
    inline void defragment() {
        this->allocator.defragment();
    }

    /* Get the total amount of memory consumed by the list. */
    inline size_t nbytes() const noexcept {
        return sizeof(*this) + sizeof(Node) * allocator.capacity;
    }

    /* Get the current specialization for Python objects within the list. */
    inline PyObject* specialization() const {
        return allocator.specialization;
    }

    /* Enforce strict type checking for elements of this list. */
    inline void specialize(PyObject* spec) {
        static_assert(
            std::is_convertible_v<Value, PyObject*>,
            "Cannot specialize a list that does not contain Python objects."
        );
        allocator.specialize(spec);
    }

    /////////////////////////////////
    ////    ITERATOR PROTOCOL    ////
    /////////////////////////////////

    /* NOTE: these methods are called automatically by the `iter()` utility function
     * when traversing a linked data structure.  Users should never need to call them
     * directly - `iter()` should always be preferred for compatibility with Python and
     * other C++ containers, as well as its more streamlined/intuitive interface.
     */

    /* Return a mutable forward iterator to the head of a view. */
    Iterator<Direction::forward> begin() {
        if (head() == nullptr) {
            return end();
        }
        Node* next = head()->next();
        return Iterator<Direction::forward>(*this, nullptr, head(), next);
    }

    /* Return a mutable forward iterator to terminate a view. */
    Iterator<Direction::forward> end() {
        return Iterator<Direction::forward>(*this);
    }

    /* Return a mutable reverse iterator to the tail of a view. */
    Iterator<Direction::backward> rbegin() {
        if (tail() == nullptr) {
            return rend();
        }

        // if list is doubly-linked, we can just use the prev pointer to get neighbors
        if constexpr (Node::doubly_linked) {
            Node* prev = tail()->prev();
            return Iterator<Direction::backward>(*this, prev, tail(), nullptr);

        // Otherwise, we have to build a temporary stack of prev pointers
        } else {
            std::stack<Node*> prev;
            prev.push(nullptr);  // stack always has at least one element (nullptr)
            Node* temp = head();
            while (temp != tail()) {
                prev.push(temp);
                temp = temp->next();
            }
            return Iterator<Direction::backward>(
                *this, std::move(prev), tail(), nullptr
            );
        }
    }

    /* Return a mutable reverse iterator to terminate a view. */
    Iterator<Direction::backward> rend() {
        return Iterator<Direction::backward>(*this);
    }

    /* Return a const forward iterator to the head of a const view. */
    ConstIterator<Direction::forward> begin() const { return cbegin(); }

    /* Return a const forward iterator to terminate a const view. */
    ConstIterator<Direction::forward> end() const { return cend(); }

    /* Return a const reverse iterator to the tail of a const view. */
    ConstIterator<Direction::backward> rbegin() const { return crbegin(); }

    /* Return a const reverse iterator to terminate a const view. */
    ConstIterator<Direction::backward> rend() const { return crend(); }

    /* Return a const forward iterator to the head of a view. */
    ConstIterator<Direction::forward> cbegin() const {
        if (head() == nullptr) {
            return cend();
        }
        Node* next = head()->next();
        return ConstIterator<Direction::forward>(*this, nullptr, head(), next);
    }

    /* Return a const forward iterator to terminate thae view. */
    ConstIterator<Direction::forward> cend() const {
        return ConstIterator<Direction::forward>(*this);
    }

    /* Return a const reverse iterator to the tail of a view. */
    ConstIterator<Direction::backward> crbegin() const {
        if (tail() == nullptr) {
            return crend();
        }

        // if list is doubly-linked, we can just use the prev pointer to get neighbors
        if constexpr (Node::doubly_linked) {
            Node* prev = tail()->prev();
            return ConstIterator<Direction::backward>(*this, prev, tail(), nullptr);

        // Otherwise, we have to build a temporary stack of prev pointers
        } else {
            std::stack<Node*> prev;
            prev.push(nullptr);  // stack always has at least one element (nullptr)
            Node* temp = head();
            while (temp != tail()) {
                prev.push(temp);
                temp = temp->next();
            }
            return ConstIterator<Direction::backward>(
                *this, std::move(prev), tail(), nullptr
            );
        }
    }

    /* Return a const reverse iterator to terminate a view. */
    ConstIterator<Direction::backward> crend() const {
        return ConstIterator<Direction::backward>(*this);
    }

protected:
    mutable Allocator allocator;  // low-level memory management
};


////////////////////////
////    LISTVIEW    ////
////////////////////////


/* A linked data structure that uses a dynamic array to store nodes in sequential
order.

ListViews use a similar memory layout to std::vector and the built-in Python list.
They initially allocate a small array of nodes (8 in this case), and advance an
internal pointer whenever a new node is constructed.  When the array is full, the view
allocates a new array with twice the size and moves the old nodes into the new array.
This strategy is significantly more efficient than allocating nodes individually on the
heap, which is typical for linked lists and incurs a large overhead for each
allocation.  This sequential memory layout also avoids heap fragmentation, improving
cache locality and overall performance as a result.  This gives the list amortized O(1)
insertion time comparable to std::vector or its Python equivalent, and much faster in
general than std::list.

Whenever a node is removed from the list, its memory is returned to the allocator array
and inserted into a linked list of recycled nodes.  This list is composed directly from
the removed nodes themselves, avoiding auxiliary data structures and associated memory
overhead.  When a new node is constructed, the free list is checked to see if there are
any available nodes.  If there are, the node is removed from the free list and reused.
Otherwise, a new node is allocated from the array, causing it to grow if necessary.
Conversely, if a linked list's size drops to below 1/4 its capacity, the allocator
array will shrink to half its current size, providing dynamic sizing.

As an additional optimization, linked lists prefer to store their nodes in strictly
sequential order, which ensures optimal alignment at the hardware level.  This is
accomplished by transferring nodes in their current list order whenever we grow or
shrink the internal array.  This automatically defragments the list periodically as
elements are added and removed, requiring no additional overhead beyond an ordinary
copy.  If desired, this process can also be triggered manually via the defragment()
method for on-demand optimization.
*/
template <typename NodeType = DoubleNode<PyObject*>>
class ListView : public BaseView<ListView<NodeType>, ListAllocator<NodeType>> {
    using Base = BaseView<ListView<NodeType>, ListAllocator<NodeType>>;

public:
    // inherit constructors
    using Base::Base;
    using Base::operator=;

    /* NOTE: we don't need any further implementation since ListView is the minimal
     * valid view configuration.  Other than an optimized memory management strategy,
     * it does not add any additional functionality, and does not assume any specific
     * capabilities of the underlying nodes.
     */
};


///////////////////////
////    SETVIEW    ////
///////////////////////


/* A linked data structure that uses a hash table to allocate and store nodes.

SetViews use a similar memory layout to std::unordered_set and the built-in Python set.
They begin by allocating a small array of nodes (16 in this case), and placing nodes
into the array using std::hash to determine their position.  If two nodes hash to the
same index, then the collision is resolved using open addressing via double hashing.
This strategy prevents clustering and ensures that items are relatively evenly
distributed across the array, which is important for optimal performance.  Additionally,
the maximum load factor before growing the array is 0.5, limiting collisions and
ensuring fast lookup times in the average case.  The minimum load factor before
shrinking the array is 0.125, providing hysteresis to prevent thrashing when the array
is near its minimum/maximum size.

Whenever a node is removed from the set, its corresponding index is marked as a
tombstone in an auxiliary bit set, indicating that it is available for reuse without
interfering with normal collision resolution.  These tombstones are skipped over during
searches and removals, and are automatically cleared when the set is resized or
defragmented, or if their concentration exceeds 1/8th of the table size.  Tombstones
can also be gradually replaced if they are encountered during insertion, delaying the
need for a full rehash.

Combining the allocator with a hash table like this provides a number of advantages
over using a separate data structure.  First, it limits memory overhead to just 2 extra
bits per node (for the required bit flags), which is significantly less than retaining
a separate hash table in addition to the nodes themselves.  Second, it guarantees (at
an algorithmic level) that the set invariants are never violated at any point.
Non-hashable items will simply fail to compile, and duplicate items will be rejected by
the allocator at runtime.  This is in contrast to maintaining a separate hash table,
which requires manual synchronization to ensure that the data structures remain
consistent with one another.  Finally, it removes an extra layer of indirection during
lookups, which improves cache locality and overall performance. */
template <typename NodeType = DoubleNode<PyObject*>>
class SetView : public BaseView<
    SetView<Hashed<NodeType>>, HashAllocator<Hashed<NodeType>>
> {
    using Base = BaseView<SetView<Hashed<NodeType>>, HashAllocator<Hashed<NodeType>>>;

public:
    using Node = Hashed<NodeType>;

    // inherit constructors
    using Base::Base;
    using Base::operator=;

    /* Search the set for a particular node/value. */
    template <typename T>
    inline Node* search(T* key) const {
        return this->allocator.search(key);
    }

};


//     /* A proxy class that allows for operations relative to a particular value
//     within the set. */
//     class RelativeProxy {
//     public:
//         using View = SetView<NodeType, Allocator>;
//         using Node = View::Node;

//         View* view;
//         Node* sentinel;
//         Py_ssize_t offset;

//         // TODO: truncate could be handled at the proxy level.  It would just be
//         // another constructor argument

//         /* Construct a new RelativeProxy for the set. */
//         RelativeProxy(View* view, Node* sentinel, Py_ssize_t offset) :
//             view(view), sentinel(sentinel), offset(offset)
//         {}

//         // TODO: relative() could just return a RelativeProxy by value, which would
//         // be deleted as soon as it falls out of scope.  This means we create a new
//         // proxy every time a variant method is called, but we can reuse them in a
//         // C++ context.

//         /* Execute a function with the RelativeProxy as its first argument. */
//         template <typename Func, typename... Args>
//         auto execute(Func func, Args... args) {
//             // function pointer must accept a RelativeProxy* as its first argument
//             using ReturnType = decltype(func(std::declval<RelativeProxy*>(), args...));

//             // call function with proxy
//             if constexpr (std::is_void_v<ReturnType>) {
//                 func(this, args...);
//             } else {
//                 return func(this, args...);
//             }
//         }


//         // TODO: these could maybe just get the proxy's curr(), prev(), and next()
//         // nodes, respectively.  We can then derive the other nodes from whichever one
//         // is populated.  For example, if we've already found and cached the prev()
//         // node, then curr() is just generated by getting prev()->next, and same with
//         // next() and curr()->next.  If none of the nodes are cached, then we have to
//         // iterate like we do now to find and cache them.  This means that in any
//         // situation where we need to get all three nodes, we should always start with
//         // prev().

//         /* Return the node at the proxy's current location. */
//         Node* walk(Py_ssize_t offset, bool truncate) {
//             // check for no-op
//             if (offset == 0) {
//                 return sentinel;
//             }

//             // TODO: introduce caching for the proxy's current position.  Probably
//             // need to use std::optional<Node*> for these, since nullptr might be a
//             // valid value.

//             // if we're traversing forward from the sentinel, then the process is the
//             // same for both singly- and doubly-linked lists
//             if (offset > 0) {
//                 curr = sentinel;
//                 for (Py_ssize_t i = 0; i < offset; i++) {
//                     if (curr == nullptr) {
//                         if (truncate) {
//                             return view->tail;  // truncate to end of list
//                         } else {
//                             return nullptr;  // index out of range
//                         }
//                     }
//                     curr = static_cast<Node*>(curr->next);
//                 }
//                 return curr;
//             }

//             // if the list is doubly-linked, we can traverse backward just as easily
//             if constexpr (has_prev<Node>::value) {
//                 curr = sentinel;
//                 for (Py_ssize_t i = 0; i > offset; i--) {
//                     if (curr == nullptr) {
//                         if (truncate) {
//                             return view->head;  // truncate to beginning of list
//                         } else {
//                             return nullptr;  // index out of range
//                         }
//                     }
//                     curr = static_cast<Node*>(curr->prev);
//                 }
//                 return curr;
//             }

//             // Otherwise, we have to iterate from the head of the list.  We do this
//             // using a two-pointer approach where the `lookahead` pointer is offset
//             // from the `curr` pointer by the specified number of steps.  When it
//             // reaches the sentinel, then `curr` will be at the correct position.
//             Node* lookahead = view->head;
//             for (Py_ssize_t i = 0; i > offset; i--) {  // advance lookahead to offset
//                 if (lookahead == sentinel) {
//                     if (truncate) {
//                         return view->head;  // truncate to beginning of list
//                     } else {
//                         return nullptr;  // index out of range
//                     }
//                 }
//                 lookahead = static_cast<Node*>(lookahead->next);
//             }

//             // advance both pointers until lookahead reaches sentinel
//             curr = view->head;
//             while (lookahead != sentinel) {
//                 curr = static_cast<Node*>(curr->next);
//                 lookahead = static_cast<Node*>(lookahead->next);
//             }
//             return curr;
//         }

//         /* Find the left and right bounds for an insertion. */
//         std::pair<Node*, Node*> junction(Py_ssize_t offset, bool truncate) {
//             // get the previous node for the insertion point
//             prev = walk(offset - 1, truncate);

//             // apply truncate rule
//             if (prev == nullptr) {  // walked off end of list
//                 if (!truncate) {
//                     return std::make_pair(nullptr, nullptr);  // error code
//                 }
//                 if (offset < 0) {
//                     return std::make_pair(nullptr, view->head);  // beginning of list
//                 }
//                 return std::make_pair(view->tail, nullptr);  // end of list
//             }

//             // return the previous node and its successor
//             curr = static_cast<Node*>(prev->next);
//             return std::make_pair(prev, curr);
//         }

//         /* Find the left and right bounds for a removal. */
//         std::tuple<Node*, Node*, Node*> neighbors(Py_ssize_t offset, bool truncate) {
//             // NOTE: we can't reuse junction() here because we need access to the node
//             // preceding the tail in the event that we walk off the end of the list and
//             // truncate=true.
//             curr = sentinel;

//             // NOTE: this is trivial for doubly-linked lists
//             if constexpr (has_prev<Node>::value) {
//                 if (offset > 0) {  // forward traversal
//                     next = static_cast<Node*>(curr->next);
//                     for (Py_ssize_t i = 0; i < offset; i++) {
//                         if (next == nullptr) {
//                             if (truncate) {
//                                 break;  // truncate to end of list
//                             } else {
//                                 return std::make_tuple(nullptr, nullptr, nullptr);
//                             }
//                         }
//                         curr = next;
//                         next = static_cast<Node*>(curr->next);
//                     }
//                     prev = static_cast<Node*>(curr->prev);
//                 } else {  // backward traversal
//                     prev = static_cast<Node*>(curr->prev);
//                     for (Py_ssize_t i = 0; i > offset; i--) {
//                         if (prev == nullptr) {
//                             if (truncate) {
//                                 break;  // truncate to beginning of list
//                             } else {
//                                 return std::make_tuple(nullptr, nullptr, nullptr);
//                             }
//                         }
//                         curr = prev;
//                         prev = static_cast<Node*>(curr->prev);
//                     }
//                     next = static_cast<Node*>(curr->next);
//                 }
//                 return std::make_tuple(prev, curr, next);
//             }

//             // NOTE: It gets significantly more complicated if the list is singly-linked.
//             // In this case, we can only optimize the forward traversal branch if we're
//             // advancing at least one node and the current node is not the tail of the
//             // list.
//             if (truncate && offset > 0 && curr == view->tail) {
//                 offset = 0;  // skip forward iteration branch
//             }

//             // forward iteration (efficient)
//             if (offset > 0) {
//                 prev = nullptr;
//                 next = static_cast<Node*>(curr->next);
//                 for (Py_ssize_t i = 0; i < offset; i++) {
//                     if (next == nullptr) {  // walked off end of list
//                         if (truncate) {
//                             break;
//                         } else {
//                             return std::make_tuple(nullptr, nullptr, nullptr);
//                         }
//                     }
//                     if (prev == nullptr) {
//                         prev = curr;
//                     }
//                     curr = next;
//                     next = static_cast<Node*>(curr->next);
//                 }
//                 return std::make_tuple(prev, curr, next);
//             }

//             // backward iteration (inefficient)
//             Node* lookahead = view->head;
//             for (size_t i = 0; i > offset; i--) {  // advance lookahead to offset
//                 if (lookahead == curr) {
//                     if (truncate) {  // truncate to beginning of list
//                         next = static_cast<Node*>(view->head->next);
//                         return std::make_tuple(nullptr, view->head, next);
//                     } else {  // index out of range
//                         return std::make_tuple(nullptr, nullptr, nullptr);
//                     }
//                 }
//                 lookahead = static_cast<Node*>(lookahead->next);
//             }

//             // advance both pointers until lookahead reaches sentinel
//             prev = nullptr;
//             Node* temp = view->head;
//             while (lookahead != curr) {
//                 prev = temp;
//                 temp = static_cast<Node*>(temp->next);
//                 lookahead = static_cast<Node*>(lookahead->next);
//             }
//             next = static_cast<Node*>(temp->next);
//             return std::make_tuple(prev, temp, next);
//         }

//     private:
//         // cache the proxy's current position in the set
//         Node* prev;
//         Node* curr;
//         Node* next;
//     };

//     /* Generate a proxy for a set that allows operations relative to a particular
//     sentinel value. */
//     template <typename T, typename Func, typename... Args>
//     auto relative(T* sentinel, Py_ssize_t offset, Func func, Args... args) {
//         // function pointer must accept a RelativeProxy* as its first argument
//         using ReturnType = decltype(func(std::declval<RelativeProxy*>(), args...));

//         // search for sentinel
//         Node* sentinel_node = search(sentinel);
//         if (sentinel_node == nullptr) {  // sentinel not found
//             PyErr_Format(PyExc_KeyError, "%R is not contained in the set", sentinel);
//             return nullptr;  // propagate
//         }

//         // stack-allocate a temporary proxy for the set (memory-safe)
//         RelativeProxy proxy(this, sentinel_node, offset);

//         // call function with proxy
//         if constexpr (std::is_void_v<ReturnType>) {
//             func(&proxy, args...);
//         } else {
//             return func(&proxy, args...);
//         }
//     }



// ////////////////////////
// ////    DICTVIEW    ////
// ////////////////////////


// // TODO: we can add a separate specialization for an LRU cache that is always
// // implemented as a DictView<DoubleNode, PreAllocator>.  This would allow us to
// // use a pure C++ implementation for the LRU cache, which isn't even wrapped
// // in a Cython class.  We could export this as a Cython alias for use in type
// // inference.  Maybe the instance factories have one of these as a C-level
// // member.



// // TODO: If we inherit from SetView<Mapped<NodeType>, Allocator>, then we need
// // to remove the hashing-related code from Mapped<>.


// template <typename NodeType, template <typename> class Allocator>
// class DictView {
// public:
//     using Node = Mapped<NodeType>;
//     Node* head;
//     Node* tail;
//     size_t size;

//     /* Disabled copy/move constructors.  These are dangerous because we're
//     manually managing memory for each node. */
//     DictView(const DictView& other) = delete;       // copy constructor
//     DictView& operator=(const DictView&) = delete;  // copy assignment
//     DictView(DictView&&) = delete;                  // move constructor
//     DictView& operator=(DictView&&) = delete;       // move assignment

//     /* Construct an empty DictView. */
//     DictView(Py_ssize_t max_size = -1) :
//         head(nullptr), tail(nullptr), size(0), specialization(nullptr),
//         table(), allocator(max_size) {}

//     /* Construct a DictView from an input iterable. */
//     DictView(
//         PyObject* iterable,
//         bool reverse = false,
//         PyObject* spec = nullptr,
//         Py_ssize_t max_size = -1
//     ) : head(nullptr), tail(nullptr), size(0), specialization(nullptr),
//         table(), allocator(max_size)
//     {
//         // C API equivalent of iter(iterable)
//         PyObject* iterator = PyObject_GetIter(iterable);
//         if (iterator == nullptr) {
//             throw std::invalid_argument("Value is not iterable");
//         }

//         // hold reference to specialization, if given
//         if (spec != nullptr) {
//             Py_INCREF(spec);
//         }

//         // unpack iterator into DictView
//         PyObject* item;
//         while (true) {
//             // C API equivalent of next(iterator)
//             item = PyIter_Next(iterator);
//             if (item == nullptr) { // end of iterator or error
//                 if (PyErr_Occurred()) {
//                     Py_DECREF(iterator);
//                     self_destruct();
//                     throw std::runtime_error("could not get item from iterator");
//                 }
//                 break;  // end of iterator
//             }

//             // allocate a new node and link it to the list
//             stage(item, reverse);
//             if (PyErr_Occurred()) {
//                 Py_DECREF(iterator);
//                 Py_DECREF(item);
//                 self_destruct();
//                 throw std::runtime_error("could not stage item");
//             }

//             // advance to next item
//             Py_DECREF(item);
//         }

//         // release reference on iterator
//         Py_DECREF(iterator);
//     };

//     /* Destroy a DictView and free all its resources. */
//     ~DictView() {
//         self_destruct();
//     }

//     /* Construct a new node for the list. */
//     template <typename... Args>
//     inline Node* node(PyObject* value, Args... args) const {
//         // variadic dispatch to Node::init()
//         Node* result = allocator.create(value, args...);
//         if (specialization != nullptr && result != nullptr) {
//             if (!Node::typecheck(result, specialization)) {
//                 recycle(result);  // clean up allocated node
//                 return nullptr;  // propagate TypeError()
//             }
//         }

//         return result;
//     }

//     /* Release a node, returning it to the allocator. */
//     inline void recycle(Node* node) const {
//         allocator.recycle(node);
//     }

//     /* Copy a single node in the list. */
//     inline Node* copy(Node* node) const {
//         return allocator.copy(node);
//     }

//     /* Make a shallow copy of the list. */
//     DictView<NodeType, Allocator>* copy() const {
//         DictView<NodeType, Allocator>* copied = new DictView<NodeType, Allocator>();
//         Node* old_node = head;
//         Node* new_node = nullptr;
//         Node* new_prev = nullptr;

//         // copy each node in list
//         while (old_node != nullptr) {
//             new_node = copy(old_node);  // copy node
//             if (new_node == nullptr) {  // error during copy()
//                 delete copied;  // discard staged list
//                 return nullptr;
//             }

//             // link to tail of copied list
//             copied->link(new_prev, new_node, nullptr);
//             if (PyErr_Occurred()) {  // error during link()
//                 delete copied;  // discard staged list
//                 return nullptr;
//             }

//             // advance to next node
//             new_prev = new_node;
//             old_node = static_cast<Node*>(old_node->next);
//         }

//         // return copied view
//         return copied;
//     }

//     /* Clear the list and reset the associated hash table. */
//     inline void clear() {
//         purge_list();  // free all nodes
//         table.reset();  // reset hash table to initial size
//     }

//     /* Link a node to its neighbors to form a linked list. */
//     void link(Node* prev, Node* curr, Node* next) {
//         // add node to hash table
//         table.remember(curr);
//         if (PyErr_Occurred()) {
//             return;
//         }

//         // delegate to node-specific link() helper
//         Node::link(prev, curr, next);

//         // update list parameters
//         size++;
//         if (prev == nullptr) {
//             head = curr;
//         }
//         if (next == nullptr) {
//             tail = curr;
//         }
//     }

//     /* Unlink a node from its neighbors. */
//     void unlink(Node* prev, Node* curr, Node* next) {
//         // remove node from hash table
//         table.forget(curr);
//         if (PyErr_Occurred()) {
//             return;
//         }

//         // delegate to node-specific unlink() helper
//         Node::unlink(prev, curr, next);

//         // update list parameters
//         size--;
//         if (prev == nullptr) {
//             head = next;
//         }
//         if (next == nullptr) {
//             tail = prev;
//         }
//     }

//     /* Enforce strict type checking for elements of this list. */
//     void specialize(PyObject* spec) {
//         // check the contents of the list
//         if (spec != nullptr) {
//             Node* curr = head;
//             for (size_t i = 0; i < size; i++) {
//                 if (!Node::typecheck(curr, spec)) {
//                     return;  // propagate TypeError()
//                 }
//                 curr = static_cast<Node*>(curr->next);
//             }
//             Py_INCREF(spec);
//         }

//         // replace old specialization
//         if (specialization != nullptr) {
//             Py_DECREF(specialization);
//         }
//         specialization = spec;
//     }

//     /* Get the type specialization for elements of this list. */
//     inline PyObject* get_specialization() const {
//         if (specialization != nullptr) {
//             Py_INCREF(specialization);
//         }
//         return specialization;  // return a new reference or NULL
//     }

//     /* Search for a node by its value. */
//     inline Node* search(PyObject* value) const {
//         return table.search(value);
//     }

//     /* Search for a node by its value. */
//     inline Node* search(Node* value) const {
//         return table.search(value);
//     }

//     /* Search for a node and move it to the front of the list at the same time. */
//     inline Node* lru_search(PyObject* value) {
//         // move node to head of list
//         Node* curr = table.search(value);
//         if (curr != nullptr && curr != head) {
//             if (curr == tail) {
//                 tail = static_cast<Node*>(curr->prev);
//             }
//             Node* prev = static_cast<Node*>(curr->prev);
//             Node* next = static_cast<Node*>(curr->next);
//             Node::unlink(prev, curr, next);
//             Node::link(nullptr, curr, head);
//             head = curr;
//         }

//         return curr;
//     }

//     /* Clear all tombstones from the hash table. */
//     inline void clear_tombstones() {
//         table.clear_tombstones();
//     }

//     /* Get the total amount of memory consumed by the dictionary (in bytes). */
//     inline size_t nbytes() const {
//         return allocator.nbytes() + table.nbytes() + sizeof(*this);
//     }

// private:
//     PyObject* specialization;  // specialized type for elements of this list
//     mutable Allocator<Node>allocator;  // stack allocated
//     HashTable<Node> table;  // stack allocated

//     /* Allocate a new node for the item and append it to the list, discarding
//     it in the event of an error. */
//     void stage(PyObject* item, bool reverse) {
//         // allocate a new node
//         Node* curr = node(item);
//         if (PyErr_Occurred()) {
//             // QoL - nothing has been allocated, so we don't actually free anything
//             if constexpr (DEBUG) {
//                 std::cout << "    -> free: " << repr(item) << std::endl;
//             }
//             return;
//         }

//         // link the node to the staged list
//         if (reverse) {
//             link(nullptr, curr, head);
//         } else {
//             link(tail, curr, nullptr);
//         }
//         if (PyErr_Occurred()) {  // node already exists
//             recycle(curr);
//             return;
//         }
//     }

//     /* Clear all nodes in the list. */
//     void purge_list() {
//         // NOTE: this does not reset the hash table, and is therefore unsafe.
//         // It should only be used to destroy a DictView or clear its contents.
//         Node* curr = head;  // store temporary reference to head

//         // reset list parameters
//         head = nullptr;
//         tail = nullptr;
//         size = 0;

//         // recycle all nodes
//         while (curr != nullptr) {
//             Node* next = static_cast<Node*>(curr->next);
//             recycle(curr);
//             curr = next;
//         }
//     }

//     /* Release the resources being managed by the DictView. */
//     inline void self_destruct() {
//         // NOTE: allocator and table are stack allocated, so they don't need to
//         // be freed here.  Their destructors will be called automatically when
//         // the DictView is destroyed.
//         purge_list();
//         if (specialization != nullptr) {
//             Py_DECREF(specialization);
//         }
//     }

// };


///////////////////////////
////    VIEW TRAITS    ////
///////////////////////////


/* A collection of SFINAE traits for inspecting view types at compile time. */
template <typename ViewType>
class ViewTraits {

    /* Detects whether the templated type has a search(Value&) method, indicating
    set-like behavior. */
    struct _is_setlike {
        /* Specialization for types that have a search(Value&) method. */
        template <typename T, typename Value = typename T::Value>
        static constexpr auto test(T* t) -> decltype(
            t->search(std::declval<Value>()),
            std::true_type()
        );

        /* Fallback for types that don't have a search() method. */
        template <typename T>
        static constexpr auto test(...) -> std::false_type;

        static constexpr bool value = decltype(test<ViewType>(nullptr))::value;
    };

    /* Detects whether the templated type has a lookup(Value&) method, indicating
    dict-like behavior. */
    struct _is_dictlike {
        /* Specialization for types that have a lookup(Value&) method. */
        template <typename T, typename Value = typename T::Value>
        static constexpr auto test(T* t) -> decltype(
            t->lookup(std::declval<Value>()),
            std::true_type()
        );

        /* Fallback for types that don't have a lookup() method. */
        template <typename T>
        static constexpr auto test(...) -> std::false_type;

        static constexpr bool value = decltype(test<ViewType>(nullptr))::value;
    };

public:
    static constexpr bool is_setlike = _is_setlike::value;
    static constexpr bool is_dictlike = _is_dictlike::value;
};


///////////////////////////////
////    VIEW DECORATORS    ////
///////////////////////////////


// TODO: Sorted<> becomes a decorator for a view, not a node.  It automatically
// converts a view of any type into a sorted view, which stores its nodes in a
// skip list.  This makes the sortedness immutable, and blocks operations that
// would unsort the list.  Every node in the list is decorated with a key value
// that is supplied by the user.  This key is provided in the constructor, and
// is cached on the node itself under a universal `key` attribute.  The SortKey
// template parameter defines what is stored in this key, and under what
// circumstances it is modified.

// using MFUCache = typename Sorted<DictView<DoubleNode>, Frequency, Descending>;

// This would create a doubly-linked skip list where each node maintains a
// value, mapped value, frequency count, hash, and prev/next pointers.  The
// view itself would maintain a hash map for fast lookups.  If the default
// SortKey is used, then we can also make the the index() method run in log(n)
// by exploiting the skip list.  These can be specific overloads in the methods
// themselves.

// This decorator can be extended to any of the existing views.



// template <
//     template <typename> class ViewType,
//     typename NodeType,
//     typename SortKey = Value,
//     typename SortOrder = Ascending
// >
// class Sorted : public ViewType<NodeType> {
// public:
//     /* A node decorator that maintains vectors of next and prev pointers for use in
//     sorted, skip list-based data structures. */
//     struct Node : public ViewType::Node {
//         std::vector<Node*> skip_next;
//         std::vector<Node*> skip_prev;

//         /* Initialize a newly-allocated node. */
//         inline static Node* init(Node* node, PyObject* value) {
//             node = static_cast<Node*>(NodeType::init(node, value));
//             if (node == nullptr) {  // Error during decorated init()
//                 return nullptr;  // propagate
//             }

//             // NOTE: skip_next and skip_prev are stack-allocated, so there's no
//             // need to initialize them here.

//             // return initialized node
//             return node;
//         }

//         /* Initialize a copied node. */
//         inline static Node* init_copy(Node* new_node, Node* old_node) {
//             // delegate to templated init_copy() method
//             new_node = static_cast<Node*>(NodeType::init_copy(new_node, old_node));
//             if (new_node == nullptr) {  // Error during templated init_copy()
//                 return nullptr;  // propagate
//             }

//             // copy skip pointers
//             new_node->skip_next = old_node->skip_next;
//             new_node->skip_prev = old_node->skip_prev;
//             return new_node;
//         }

//         /* Tear down a node before freeing it. */
//         inline static void teardown(Node* node) {
//             node->skip_next.clear();  // reset skip pointers
//             node->skip_prev.clear();
//             NodeType::teardown(node);
//         }

//         // TODO: override link() and unlink() to update skip pointers and maintain
//         // sorted order
//     }
// }


// ////////////////////////
// ////    POLICIES    ////
// ////////////////////////


// // TODO: Value and Frequency should be decorators for nodes to give them full
// // type information.  They can even wrap


// /* A SortKey that stores a reference to a node's value in its key. */
// struct Value {
//     /* Decorate a freshly-initialized node. */
//     template <typename Node>
//     inline static void decorate(Node* node) {
//         node->key = node->value;
//     }

//     /* Clear a node's sort key. */
//     template <typename Node>
//     inline static void undecorate(Node* node) {
//         node->key = nullptr;
//     }
// };


// /* A SortKey that stores a frequency counter as a node's key. */
// struct Frequency {
//     /* Initialize a node's sort key. */
//     template <typename Node>
//     inline static void decorate(Node* node) {
//         node->key = 0;
//     }

//     /* Clear a node's sort key */

// };


// /* A Sorted<> policy that sorts nodes in ascending order based on key. */
// template <typename SortValue>
// struct Ascending {
//     /* Check whether two keys are in sorted order relative to one another. */
//     template <typename KeyValue>
//     inline static bool compare(KeyValue left, KeyValue right) {
//         return left <= right;
//     }

//     /* A specialization for compare to use with Python objects as keys. */
//     template <>
//     inline static bool compare(PyObject* left, PyObject* right) {
//         return PyObject_RichCompareBool(left, right, Py_LE);  // -1 signals error
//     }
// };


// /* A specialized version of Ascending that compares PyObject* references. */
// template <>
// struct Ascending<Value> {
    
// };


// /* A Sorted<> policy that sorts nodes in descending order based on key. */
// struct Descending {
//     /* Check whether two keys are in sorted order relative to one another. */
//     template <typename KeyValue>
//     inline static int compare(KeyValue left, KeyValue right) {
//         return left >= right;
//     }

//     /* A specialization for compare() to use with Python objects as keys. */
//     template <>
//     inline static int compare(PyObject* left, PyObject* right) {
//         return PyObject_RichCompareBool(left, right, Py_GE);  // -1 signals error
//     }
// };


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_VIEW_H
