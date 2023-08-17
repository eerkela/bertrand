// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_ALLOCATE_H
#define BERTRAND_STRUCTS_CORE_ALLOCATE_H

#include <cstddef>  // size_t
#include <cstdlib>  // malloc(), free()
#include <queue>  // std::queue
#include <stdexcept>  // std::invalid_argument
#include <Python.h>


// In the future, we could look into dynamically allocating blocks of nodes
// using block allocation, but then we'd need to keep track of which blocks are
// full and which are empty, and allocate/remove them as needed.  This would
// be particularly complicated if nodes are removed from the middle of a block,
// which could lead to the blocks becoming sparse.  We'd need to figure out a
// way to consolidate these blocks, which would be a lot of work.


/////////////////////////
////    CONSTANTS    ////
/////////////////////////


/* DEBUG=TRUE adds print statements for every call to malloc()/free() in order
to help catch memory leaks. */
const bool DEBUG = false;


/* Every View maintains a freelist of blank nodes that can be reused for fast
allocation/deallocation. */
const unsigned int FREELIST_SIZE = 32;  // max size of each View's freelist


////////////////////////////////
////    HELPER FUNCTIONS    ////
////////////////////////////////


/* Get the Python repr() of an arbitrary PyObject* as a C string. */
const char* repr(PyObject* obj) {
    if (obj == nullptr) {
        return "NULL";
    }

    // call repr()
    PyObject* py_repr = PyObject_Repr(obj);
    if (py_repr == nullptr) {
        return "NULL";
    }

    // convert to UTF-8
    const char* c_repr = PyUnicode_AsUTF8(py_repr);
    if (c_repr == nullptr) {
        return "NULL";
    }

    Py_DECREF(py_repr);
    return c_repr;
}


////////////////////
////    BASE    ////
////////////////////


/* Common interface for all node allocators. */
template <typename Node>
class BaseAllocator {
protected:
    size_t alive;  // number of currently-alive nodes

    /* A wrapper around malloc() that can help catch memory leaks. */
    inline Node* malloc_node(PyObject* value) {
        // print allocation/deallocation messages if DEBUG=TRUE
        if constexpr (DEBUG) {
            printf("    -> malloc: %s\n", repr(value));
        }

        // malloc()
        ++alive;
        return static_cast<Node*>(malloc(sizeof(Node)));  // may be NULL
    }

    /* A wrapper around free() that can help catch memory leaks. */
    inline void free_node(Node* node) {
        // print allocation/deallocation messages if DEBUG=TRUE
        if constexpr (DEBUG) {
            printf("    -> free: %s\n", repr(node->value));
        }

        // free()
        --alive;
        free(node);
    }

public:

    /* Copy constructors. These are disabled for the sake of efficiency,
    forcing us not to copy data unnecessarily. */
    BaseAllocator(const BaseAllocator& other) = delete;         // copy constructor
    BaseAllocator& operator=(const BaseAllocator&) = delete;    // copy assignment

    /* Empty constructor.  This just initializes the `alive` field to zero. */
    BaseAllocator() : alive(0) {};

    /* Move ownership from one Allocator to another (move constructor). */
    BaseAllocator(BaseAllocator&& other) : alive(other.alive) {
        other.alive = 0;
    }

    /* Move ownership from one Allocator to another (move assignment). */
    BaseAllocator& operator=(BaseAllocator&& other) {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        this->alive = other.alive;
        other.alive = 0;
        return *this;
    }

    /* Allocator interface.  Every subclass must implement these 3 methods. */
    template <typename... Args>
    Node* create(Args... args);
    Node* copy(Node* node);
    void recycle(Node* node);

    /* Return the number of currently-allocated nodes. */
    inline size_t allocated() {
        return alive;
    };

    /* Return the total amount of memory being managed by this allocator. */
    inline size_t nbytes() {
        return alive * sizeof(Node) + sizeof(*this);
    };

};


//////////////////////
////    DIRECT    ////
//////////////////////


/* A factory for the templated node that directly allocates and frees each node. */
template <typename Node>
class DirectAllocator : public BaseAllocator<Node> {
public:

    /* Create a DirectAllocator for the given node. */
    DirectAllocator(ssize_t max_size) : BaseAllocator<Node>() {}

    /* Initialize a new node for the list. */
    template <typename... Args>
    inline Node* create(PyObject* value, Args... args) {
        // allocate a blank node
        Node* node = this->malloc_node(value);
        if (node == nullptr) {  // malloc() failed
            PyErr_NoMemory();
            return nullptr;  // propagate
        }

        // variadic dispatch to one of the node's init() methods
        Node* initialized = Node::init(node, value, args...);
        if (initialized == nullptr) {  // Error during init()
            this->free_node(node);
        }

        // return initialized node
        return initialized;
    }

    /* Copy an existing node. */
    inline Node* copy(Node* node) {
        Node* copied = this->malloc_node(node->value);
        if (copied == nullptr) {  // malloc() failed
            PyErr_NoMemory();
            return nullptr;  // propagate
        }

        // dispatch to node's init_copy() method
        Node* initialized = Node::init_copy(copied, node);
        if (initialized == nullptr) {  // Error during init_copy()
            this->free_node(copied);
        }

        // return initialized node
        return initialized;
    }

    /* Free a node, returning its resources to the allocator. */
    inline void recycle(Node* node) {
        Node::teardown(node);
        this->free_node(node);
    }

};


/////////////////////////
////    FREE LIST    ////
/////////////////////////


/* A factory for the templated node that uses a freelist to manage life cycles. */
template <typename Node>
class FreeListAllocator : public BaseAllocator<Node> {
private:
    std::queue<Node*> freelist;

    /* Pop a node from the freelist or allocate a new one directly. */
    inline Node* pop_node(PyObject* value) {
        Node* node;
        if (!freelist.empty()) {
            node = freelist.front();  // pop from freelist
            freelist.pop();
        } else {
            node = this->malloc_node(value);  // malloc()
        }
        return node;  // may be NULL
    }

    /* Push a node to the freelist or free it directly. */
    inline void push_node(Node* node) {
        if (freelist.size() < FREELIST_SIZE) {
            freelist.push(node);  // push to freelist
        } else {
            this->free_node(node);  // free()
        }
    }

public:

    /* Create a freelist allocator for the given node. */
    FreeListAllocator(ssize_t max_size) : BaseAllocator<Node>() {}

    /* Move ownership from one FreeListAllocator to another (move constructor). */
    FreeListAllocator(FreeListAllocator&& other) :
        BaseAllocator<Node>(std::move(other)), freelist(std::move(other.freelist))
    {
        other.freelist = std::queue<Node*>();
    }

    /* Move ownership from one FreeListAllocator to another (move assignment). */
    FreeListAllocator& operator=(FreeListAllocator&& other) {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // call base class's move assignment operator
        BaseAllocator<Node>::operator=(std::move(other));

        // clear current freelist
        this->purge();

        // move other into self
        freelist = std::move(other.freelist);

        // reset other
        other.freelist = std::queue<Node*>();
        return *this;
    }

    /* Destroy the freelist and release all nodes within it. */
    ~FreeListAllocator() {
        purge();
    }

    /* Initialize a new node for the list. */
    template <typename... Args>
    inline Node* create(PyObject* value, Args... args) {
        // get blank node
        Node* node = pop_node(value);
        if (node == nullptr) {  // malloc() failed
            PyErr_NoMemory();  // set MemoryError
            return nullptr;
        }

        // NOTE: variadic dispatch to one of the node's init() methods
        node = Node::init(node, value, args...);
        if (node == nullptr) {
            push_node(node);  // init failed, push to freelist
        }

        // return initialized node
        return node;
    }

    /* Copy an existing node. */
    inline Node* copy(Node* old_node) {
        // get blank node
        Node* new_node = pop_node(old_node->value);
        if (new_node == nullptr) {  // malloc() failed
            PyErr_NoMemory();  // set MemoryError
            return nullptr;
        }

        // dispatch to node's init_copy() method
        new_node = Node::init_copy(new_node, old_node);
        if (new_node == nullptr) {
            push_node(new_node);  // init failed, push to freelist
        }

        // return initialized node
        return new_node;
    }

    /* Free a node, returning its resources to the allocator. */
    inline void recycle(Node* node) {
        Node::teardown(node);  // release references
        push_node(node);  // push to freelist
    }

    /* Release all nodes the freelist. */
    inline void purge() {
        while (!freelist.empty()) {
            Node* node = freelist.front();
            freelist.pop();
            this->free_node(node);
        }
    }

    /* Get the number of nodes stored in the freelist. */
    inline size_t reserved() const {
        return freelist.size();
    }

};


////////////////////
////    POOL    ////
////////////////////


/* A factory for the templated node that preallocates a contiguous block of nodes. */
template <typename Node>
class PreAllocator : public BaseAllocator<Node> {
private:
    size_t max_size;  // length of block
    Node* block;  // contiguous block of nodes
    std::queue<Node*> available;  // stack of open node addresses

public:

    /* Pre-allocate the given number of nodes as a contiguous block. */
    PreAllocator(ssize_t size) {
        // check for invalid size
        if (size < 0) {
            throw std::invalid_argument("PreAllocator size must be >= 0");
        }
        max_size = static_cast<size_t>(size);

        // print allocation/deallocation messages if DEBUG=TRUE
        if constexpr (DEBUG) {
            printf("    -> malloc: %zu preallocated nodes\n", max_size);
        }

        // allocate a contiguous block of nodes
        block = static_cast<Node*>(malloc(max_size * sizeof(Node)));
        if (block == nullptr) {  // malloc() failed
            throw std::bad_alloc();
        }

        // build stack of open node addresses
        for (size_t i = 0; i < max_size; ++i) {
            available.push(&block[i]);
        }
        this->alive = max_size;
    }

    /* Move ownership from one PreAllocator to another (move constructor). */
    PreAllocator(PreAllocator&& other) :
        BaseAllocator<Node>(std::move(other)), max_size(other.max_size),
        block(other.block), available(std::move(other.available))
    {
        // reset other
        other.max_size = 0;
        other.block = nullptr;
        other.available = std::queue<Node*>();
    }

    /* Move ownership from one PreAllocator to another (move assignment). */
    PreAllocator& operator=(PreAllocator&& other) {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // call base class's move assignment operator
        BaseAllocator<Node>::operator=(std::move(other));

        // clear current block
        free(block);

        // move other into self
        max_size = other.max_size;
        block = other.block;
        available = std::move(other.available);

        // reset other
        other.max_size = 0;
        other.block = nullptr;
        other.available = std::queue<Node*>();
        return *this;
    }

    /* Free the pre-allocated nodes as a contiguous block. */
    ~PreAllocator() {
        // print allocation/deallocation messages if DEBUG=TRUE
        if constexpr (DEBUG) {
            printf("    -> free: %zu preallocated nodes\n", max_size);
        }

        free(block);
    }

    /* Initialize a new node for the list. */
    template <typename... Args>
    inline Node* create(PyObject* value, Args... args) {
        // check if block is full
        if (available.empty()) {
            PyErr_Format(
                PyExc_IndexError,
                "Could not allocate a new node: list exceeded its maximum size (%zu)",
                max_size
            );
            return nullptr;  // propagate
        }

        // get blank node
        Node* node = available.front();
        available.pop();

        // NOTE: variadic dispatch to one of the node's init() methods
        node = Node::init(node, value, args...);
        if (node == nullptr) {
            available.push(node);  // init failed, return to block
        }

        // return initialized node
        return node;
    }

    /* Copy an existing node. */
    inline Node* copy(Node* old_node) {
        // check if block is full
        if (available.empty()) {
            PyErr_Format(
                PyExc_IndexError,
                "Could not allocate a new node: list exceeded its maximum size (%zu)",
                max_size
            );
            return nullptr;  // propagate
        }

        // get blank node
        Node* new_node = available.front();
        available.pop();

        // dispatch to node's init_copy() method
        new_node = Node::init_copy(new_node, old_node);
        if (new_node == nullptr) {
            available.push(new_node);  // init failed, return to block
        }

        // return initialized node
        return new_node;
    }

    /* Free a node, returning its resources to the allocator. */
    inline void recycle(Node* node) {
        Node::teardown(node);  // release references
        available.push(node);  // return to block
    }

    /* Get the number of nodes waiting to be allocated. */
    inline size_t reserved() const {
        return available.size();
    }

};


#endif  // BERTRAND_STRUCTS_CORE_ALLOCATE_H include guard
