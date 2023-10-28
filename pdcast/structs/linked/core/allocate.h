// include guard: BERTRAND_STRUCTS_LINKED_ALLOCATE_H
#ifndef BERTRAND_STRUCTS_LINKED_ALLOCATE_H
#define BERTRAND_STRUCTS_LINKED_ALLOCATE_H

#include <cstddef>  // size_t
#include <cstdlib>  // malloc(), free()
#include <iostream>  // std::cout, std::endl
#include <optional>  // std::optional
#include <sstream>  // std::ostringstream
#include <stdexcept>  // std::invalid_argument
#include <Python.h>  // CPython API
#include "../util/except.h"  // catch_python(), type_error()
#include "../util/math.h"  // next_power_of_two()
#include "../util/repr.h"  // repr()


namespace bertrand {
namespace structs {
namespace linked {


/////////////////////////
////    CONSTANTS    ////
/////////////////////////


/* DEBUG=TRUE adds print statements for every call to malloc()/free() in order
to help catch memory leaks. */
const bool DEBUG = true;


////////////////////
////    BASE    ////
////////////////////


/* Empty tag class marking a node allocator for a linked data structure.

NOTE: this class is inherited by all allocators, and can be used for easy SFINAE
checks via std::is_base_of, without requiring any foreknowledge of template
parameters. */
class AllocatorTag {};


/* Base class that implements shared functionality for all allocators and provides the
minimum necessary attributes for compatibility with higher-level views. */
template <typename NodeType>
class BaseAllocator : public AllocatorTag {
public:
    using Node = NodeType;

protected:
    // TODO: do this with NodeTraits<>
    // static constexpr bool is_pyobject = std::is_convertible_v<
    //     typename Node::Value,
    //     PyObject*
    // >;

    /* Allocate a contiguous block of uninitialized nodes with the specified size. */
    inline static Node* allocate_array(size_t capacity) {
        Node* result = static_cast<Node*>(malloc(capacity * sizeof(Node)));
        if (result == nullptr) {
            throw std::bad_alloc();
        }
        return result;
    }

    /* Initialize an uninitialized node for use in the list. */
    template <typename... Args>
    inline void init_node(Node* node, Args&&... args) {
        using util::repr;

        // variadic dispatch to node constructor
        new (node) Node(std::forward<Args>(args)...);

        // check python specialization if enabled
        if (specialization != nullptr && !node->typecheck(specialization)) {
            std::ostringstream msg;
            msg << repr(node->value()) << " is not of type ";
            msg << repr(specialization);
            node->~Node();  // in-place destructor
            throw util::type_error(msg.str());
        }

        if constexpr (DEBUG) {
            std::cout << "    -> create: " << repr(node->value()) << std::endl;
        }
    }

    /* Destroy all nodes contained in the list. */
    inline void destroy_list() noexcept {
        using util::repr;

        Node* curr = head;
        while (curr != nullptr) {
            Node* next = curr->next();
            if constexpr (DEBUG) {
                std::cout << "    -> recycle: " << repr(curr->value()) << std::endl;
            }
            curr->~Node();  // in-place destructor
            curr = next;
        }
    }

public:
    Node* head;  // head of the list
    Node* tail;  // tail of the list
    size_t capacity;  // number of nodes in the array
    size_t occupied;  // number of nodes currently in use - equivalent to list.size()
    bool frozen;  // indicates if the array is frozen at its current capacity
    PyObject* specialization;  // type specialization for PyObject* values

    // TODO: specialization and specialize() might be conditionally compiled based on
    // whether the node stores PyObject* values, as determined from NodeTraits<>.

    /* Create an allocator with an optional fixed size. */
    BaseAllocator(std::optional<size_t> capacity, PyObject* specialization, size_t default_capacity) :
        head(nullptr),
        tail(nullptr),
        capacity(capacity.value_or(default_capacity)),
        occupied(0),
        frozen(capacity.has_value()),
        specialization(specialization)
    {
        if constexpr (DEBUG) {
            std::cout << "    -> allocate: " << this->capacity << " nodes" << std::endl;
        }
        if (specialization != nullptr) {
            Py_INCREF(specialization);
        }
    }

    /* Copy constructor. */
    BaseAllocator(const BaseAllocator& other) :
        head(nullptr),
        tail(nullptr),
        capacity(other.capacity),
        occupied(other.occupied),
        frozen(other.frozen),
        specialization(other.specialization)
    {
        if constexpr (DEBUG) {
            std::cout << "    -> allocate: " << capacity << " nodes" << std::endl;
        }
        Py_XINCREF(specialization);
    }

    /* Move constructor. */
    BaseAllocator(BaseAllocator&& other) noexcept :
        head(other.head),
        tail(other.tail),
        capacity(other.capacity),
        occupied(other.occupied),
        frozen(other.frozen),
        specialization(other.specialization)
    {
        other.head = nullptr;
        other.tail = nullptr;
        other.capacity = 0;
        other.occupied = 0;
        other.frozen = false;
        other.specialization = nullptr;
    }

    /* Copy assignment operator. */
    BaseAllocator& operator=(const BaseAllocator& other) {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // destroy current
        Py_XDECREF(specialization);
        if (head != nullptr) {
            destroy_list();  // calls destructors
            head = nullptr;
            tail = nullptr;
        }
        if constexpr (DEBUG) {
            std::cout << "    -> deallocate: " << capacity << " nodes" << std::endl;
        }

        // copy from other
        capacity = other.capacity;
        occupied = other.occupied;
        frozen = other.frozen;
        specialization = Py_XNewRef(other.specialization);

        return *this;
    }

    /* Move assignment operator. */
    BaseAllocator& operator=(BaseAllocator&& other) noexcept {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // destroy current
        Py_XDECREF(specialization);
        if (head != nullptr) {
            destroy_list();  // calls destructors
        }
        if constexpr (DEBUG) {
            std::cout << "    -> deallocate: " << capacity << " nodes" << std::endl;
        }

        // transfer ownership
        head = other.head;
        tail = other.tail;
        capacity = other.capacity;
        occupied = other.occupied;
        frozen = other.frozen;
        specialization = other.specialization;

        // reset other
        other.head = nullptr;
        other.tail = nullptr;
        other.capacity = 0;
        other.occupied = 0;
        other.frozen = false;
        other.specialization = nullptr;
        return *this;
    }

    /* Destroy an allocator and release its resources. */
    ~BaseAllocator() noexcept {
        Py_XDECREF(specialization);
    }

    /* Enforce strict type checking for python values within the list. */
    void specialize(PyObject* spec) {
        // handle null assignment
        if (spec == nullptr) {
            if (specialization != nullptr) {
                Py_DECREF(specialization);  // release old spec
                specialization = nullptr;
            }
            return;
        }

        // early return if new spec is same as old spec
        if (specialization != nullptr) {
            int comp = PyObject_RichCompareBool(spec, specialization, Py_EQ);
            if (comp == -1) {  // comparison raised an exception
                throw util::catch_python<util::type_error>();
            } else if (comp == 1) {
                return;
            }
        }

        // check the contents of the list
        Node* curr = head;
        while (curr != nullptr) {
            if (!curr->typecheck(spec)) {
                throw util::type_error("node type does not match specialization");
            }
            curr = curr->next();
        }

        // replace old specialization
        Py_INCREF(spec);
        if (specialization != nullptr) {
            Py_DECREF(specialization);
        }
        specialization = spec;
    }

};


////////////////////
////    LIST    ////
////////////////////


/* A custom allocator that uses a dynamic array to manage memory for each node. */
template <typename NodeType>
class ListAllocator : public BaseAllocator<NodeType> {
public:
    using Node = NodeType;

private:
    using Base = BaseAllocator<Node>;
    static constexpr size_t DEFAULT_CAPACITY = 8;  // minimum array size

    Node* array;  // dynamic array of nodes
    std::pair<Node*, Node*> free_list;  // singly-linked list of open nodes

    /* When we allocate new nodes, we fill the dynamic array from left to right.
    If a node is removed from the middle of the array, then we add it to the free
    list.  The next time a node is allocated, we check the free list and reuse a
    node if possible.  Otherwise, we initialize a new node at the end of the
    occupied section.  If this causes the array to exceed its capacity, then we
    allocate a new array with twice the length and copy all nodes in the same order
    as they appear in the list. */

    /* Copy/move the nodes from one allocator into another. */
    template <bool copy>
    static void transfer_nodes(Node* head, Node* array) {
        // NOTE: nodes are always transferred in list order, with the head at the
        // front of the array.  This is done to aid cache locality, ensuring that
        // every subsequent node immediately follows its predecessor in memory.

        // keep track of previous node to maintain correct order
        Node* new_prev = nullptr;

        // copy over existing nodes from head -> tail
        Node* curr = head;
        size_t idx = 0;
        while (curr != nullptr) {
            // remember next node in original list
            Node* next = curr->next();

            // initialize new node in array
            Node* new_curr = &array[idx];
            if constexpr (copy) {
                new (new_curr) Node(*curr);
            } else {
                new (new_curr) Node(std::move(*curr));
            }

            // link to previous node in array
            Node::join(new_prev, new_curr);

            // advance
            new_prev = new_curr;
            curr = next;
            ++idx;
        }
    }

    /* Allocate a new array of a given size and transfer the contents of the list. */
    void resize(size_t new_capacity) {
        // allocate new array
        Node* new_array = Base::allocate_array(new_capacity);
        if constexpr (DEBUG) {
            std::cout << "    -> allocate: " << new_capacity << " nodes" << std::endl;
        }

        // move nodes into new array
        transfer_nodes<false>(this->head, new_array);

        // replace old array
        free(array);  // bypasses destructors
        if constexpr (DEBUG) {
            std::cout << "    -> deallocate: " << this->capacity << " nodes" << std::endl;
        }
        array = new_array;
        this->capacity = new_capacity;
        free_list.first = nullptr;
        free_list.second = nullptr;

        // update head/tail pointers
        if (this->occupied != 0) {
            this->head = &(new_array[0]);
            this->tail = &(new_array[this->occupied - 1]);
        }
    }

public:

    /* Create an allocator with an optional fixed size. */
    ListAllocator(std::optional<size_t> capacity, PyObject* specialization) :
        Base(capacity, specialization, DEFAULT_CAPACITY),
        array(Base::allocate_array(this->capacity)),
        free_list(std::make_pair(nullptr, nullptr))
    {}

    /* Copy constructor. */
    ListAllocator(const ListAllocator& other) :
        Base(other),
        array(Base::allocate_array(this->capacity)),
        free_list(std::make_pair(nullptr, nullptr))
    {
        // copy over existing nodes in correct list order (head -> tail)
        if (this->occupied != 0) {
            transfer_nodes<true>(other.head, array);
            this->head = &array[0];
            this->tail = &array[this->occupied - 1];
        }
    }

    /* Move constructor. */
    ListAllocator(ListAllocator&& other) noexcept :
        Base(std::move(other)),
        array(other.array),
        free_list(std::move(other.free_list))
    {
        other.array = nullptr;
        other.free_list.first = nullptr;
        other.free_list.second = nullptr;
    }

    /* Copy assignment operator. */
    ListAllocator& operator=(const ListAllocator& other) {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // invoke parent
        Base::operator=(other);

        // destroy array
        free_list.first = nullptr;
        free_list.second = nullptr;
        if (array != nullptr) {
            free(array);
        }

        // copy array
        array = Base::allocate_array(this->capacity);
        if (this->occupied != 0) {
            transfer_nodes<true>(other.head, array);
            this->head = &array[0];
            this->tail = &array[this->occupied - 1];
        }

        return *this;
    }

    /* Move assignment operator. */
    ListAllocator& operator=(ListAllocator&& other) noexcept {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // invoke parent
        Base::operator=(std::move(other));

        // destroy array
        if (array != nullptr) {
            free(array);
        }

        // transfer ownership
        array = other.array;
        free_list.first = other.free_list.first;
        free_list.second = other.free_list.second;

        // reset other
        other.array = nullptr;
        other.free_list.first = nullptr;
        other.free_list.second = nullptr;
        return *this;
    }

    /* Destroy an allocator and release its resources. */
    ~ListAllocator() noexcept {
        if (this->head != nullptr) {
            Base::destroy_list();  // calls destructors
        }
        if (array != nullptr) {
            free(array);
            if constexpr (DEBUG) {
                std::cout << "    -> deallocate: " << this->capacity << " nodes" << std::endl;
            }
        }
    }

    /* Construct a new node for the list. */
    template <typename... Args>
    Node* create(Args... args) {
        // check free list
        if (free_list.first != nullptr) {
            Node* node = free_list.first;
            Node* temp = node->next();
            try {
                Base::init_node(node, std::forward<Args>(args)...);
            } catch (...) {
                node->next(temp);  // restore free list
                throw;  // propagate
            }
            free_list.first = temp;
            if (temp == nullptr) {
                free_list.second = nullptr;
            }
            ++this->occupied;
            return node;
        }

        // check if we need to grow the array
        if (this->occupied == this->capacity) {
            if (this->frozen) {
                std::ostringstream msg;
                msg << "array cannot grow beyond size " << this->capacity;
                throw std::runtime_error(msg.str());
            }
            resize(this->capacity * 2);
        }

        // append to end of allocated section
        Node* node = &array[this->occupied++];
        Base::init_node(node, std::forward<Args>(args)...);
        return node;
    }

    /* Release a node from the list. */
    void recycle(Node* node) {
        // manually call destructor
        if constexpr (DEBUG) {
            std::cout << "    -> recycle: " << util::repr(node->value()) << std::endl;
        }
        node->~Node();

        // check if we need to shrink the array
        if (!this->frozen &&
            this->capacity != DEFAULT_CAPACITY &&
            this->occupied == this->capacity / 4
        ) {
            resize(this->capacity / 2);
        } else {
            if (free_list.first == nullptr) {
                free_list.first = node;
                free_list.second = node;
            } else {
                free_list.second->next(node);
                free_list.second = node;
            }
        }
        --this->occupied;
    }

    /* Remove all elements from a list. */
    void clear() noexcept {
        // destroy all nodes
        Base::destroy_list();

        // reset list parameters
        this->head = nullptr;
        this->tail = nullptr;
        this->occupied = 0;
        free_list.first = nullptr;
        free_list.second = nullptr;
        if (!this->frozen) {
            this->capacity = DEFAULT_CAPACITY;
            free(array);
            array = Base::allocate_array(this->capacity);
        }
    }

    /* Resize the array to house a specific number of nodes. */
    void reserve(size_t new_capacity) {
        // ensure new capacity is large enough to store all nodes
        if (new_capacity < this->occupied) {
            throw std::invalid_argument(
                "new capacity must not be smaller than current size"
            );
        }

        // handle frozen arrays
        if (this->frozen) {
            if (new_capacity <= this->capacity) {
                return;  // do nothing
            }
            std::ostringstream msg;
            msg << "array cannot grow beyond size " << this->capacity;
            throw std::runtime_error(msg.str());
        }

        // ensure array does not shrink below default capacity
        if (new_capacity <= DEFAULT_CAPACITY) {
            if (this->capacity != DEFAULT_CAPACITY) {
                resize(DEFAULT_CAPACITY);
            }
            return;
        }

        // resize to the next power of two
        size_t rounded = util::next_power_of_two(new_capacity);
        if (rounded != this->capacity) {
            resize(rounded);
        }
    }

    /* Consolidate the nodes within the array, arranging them in the same order as
    they appear within the list. */
    inline void consolidate() {
        resize(this->capacity);  // in-place resize
    }

    /* Check whether the referenced node is being managed by this allocator. */
    inline bool owns(Node* node) const noexcept {
        return node >= array && node < array + this->capacity;  // pointer arithmetic
    }

};


//////////////////////////////
////    SET/DICTIONARY    ////
//////////////////////////////


/* A custom allocator that uses a hash table to manage memory for each node. */
template <typename NodeType>
class HashAllocator : public BaseAllocator<NodeType> {
public:
    using Node = NodeType;

private:
    static constexpr size_t DEFAULT_CAPACITY = 16;  // minimum array size
    static constexpr float MAX_LOAD_FACTOR = 0.7;  // grow if load factor exceeds threshold
    static constexpr float MIN_LOAD_FACTOR = 0.2;  // shrink if load factor drops below threshold
    static constexpr float MAX_TOMBSTONES = 0.2;  // clear tombstones if threshold is exceeded
    static constexpr size_t PRIMES[29] = {  // prime numbers to use for double hashing
        // HASH PRIME   // TABLE SIZE               // AI AUTOCOMPLETE
        13,             // 16 (2**4)                13
        23,             // 32 (2**5)                23
        47,             // 64 (2**6)                53
        97,             // 128 (2**7)               97
        181,            // 256 (2**8)               193
        359,            // 512 (2**9)               389
        719,            // 1024 (2**10)             769
        1439,           // 2048 (2**11)             1543
        2879,           // 4096 (2**12)             3079
        5737,           // 8192 (2**13)             6151
        11471,          // 16384 (2**14)            12289
        22943,          // 32768 (2**15)            24593
        45887,          // 65536 (2**16)            49157
        91753,          // 131072 (2**17)           98317
        183503,         // 262144 (2**18)           196613
        367007,         // 524288 (2**19)           393241
        734017,         // 1048576 (2**20)          786433
        1468079,        // 2097152 (2**21)          1572869
        2936023,        // 4194304 (2**22)          3145739
        5872033,        // 8388608 (2**23)          6291469
        11744063,       // 16777216 (2**24)         12582917
        23488103,       // 33554432 (2**25)         25165843
        46976221,       // 67108864 (2**26)         50331653
        93952427,       // 134217728 (2**27)        100663319
        187904861,      // 268435456 (2**28)        201326611
        375809639,      // 536870912 (2**29)        402653189
        751619321,      // 1073741824 (2**30)       805306457
        1503238603,     // 2147483648 (2**31)       1610612741
        3006477127,     // 4294967296 (2**32)       3221225473
        // NOTE: HASH PRIME is the first prime number larger than 0.7 * TABLE_SIZE
    };

    Node* array;  // dynamic array of nodes
    size_t tombstones;  // number of nodes marked for deletion
    size_t prime;  // prime number used for double hashing
    unsigned char exponent;  // log2(capacity) - log2(DEFAULT_CAPACITY)

public:

    // TODO: head, tail, capacity, occupied, frozen, specialization can all be placed
    // in base class along with allocate_array(), init_node(), destroy_list(), and
    // specialize().


    /* Check whether the referenced node is being managed by this allocator. */
    inline bool owns(Node* node) const noexcept {
        return node >= array && node < array + this->capacity;  // pointer arithmetic
    }

};


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALLOCATE_H
