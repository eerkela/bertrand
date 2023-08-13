// include guard prevents multiple inclusion
#ifndef NODE_H
#define NODE_H

#include <queue>  // for std::queue
#include <type_traits>  // for std::integer_constant, std::is_base_of
#include <Python.h>  // for CPython API


// TODO: add a fixed-size allocator for lists of a given maximum size.  This
// would allocate all nodes in a single contiguous block of memory, which would
// be beneficial for cache locality.  We then never need to allocate or free
// any extra nodes.  We just keep a stack of open node addresses and push/pop
// as we go.

// The freelist is only used in case of a dynamic list.  This reduces unnecessary
// calls to malloc()/free() in cases where items are being frequently added and
// removed from the list, but comes with extra fragmentation.  That's not a
// huge problem though, and the list should still be pretty fast even in this
// case.

// In the future, we could look into dynamically allocating blocks of nodes
// using a memory pool, but then we'd need to keep track of which blocks are
// full and which are empty, and allocate/remove them as needed.  This would
// be particularly complicated if nodes are removed from the middle of a block,
// which could lead to the blocks becoming sparse.  We'd need to figure out a
// way to consolidate these blocks, which would be a lot of work.


/////////////////////////
////    CONSTANTS    ////
/////////////////////////


/* DEBUG=TRUE adds print statements for every call to malloc()/free() in order
to help catch memory leaks. */
const bool DEBUG = true;


/* Every View maintains a freelist of blank nodes that can be reused for fast
allocation/deallocation. */
const unsigned int FREELIST_SIZE = 32;  // max size of each View's freelist


/////////////////////////
////    FUNCTIONS    ////
/////////////////////////


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


/////////////////////
////    NODES    ////
/////////////////////


/* A singly-linked list node containing a single PyObject* reference. */
struct SingleNode {
    PyObject* value;
    SingleNode* next;

    /* Initialize a newly-allocated node. */
    inline static SingleNode* init(SingleNode* node, PyObject* value) {
        Py_INCREF(value);
        node->value = value;
        node->next = nullptr;
        return node;
    }

    /* Initialize a copied node. */
    inline static SingleNode* init_copy(SingleNode* new_node, SingleNode* old_node) {
        Py_INCREF(old_node->value);
        new_node->value = old_node->value;
        new_node->next = nullptr;
        return new_node;
    }

    /* Tear down a node before freeing it. */
    inline static void teardown(SingleNode* node) {
        Py_DECREF(node->value);
    }

    /* Link the node to its neighbors to form a singly-linked list. */
    inline static void link(
        SingleNode* prev,
        SingleNode* curr,
        SingleNode* next
    ) {
        if (prev != nullptr) {
            prev->next = curr;
        }
        curr->next = next;
    }

    /* Unlink the node from its neighbors. */
    inline static void unlink(
        SingleNode* prev,
        SingleNode* curr,
        SingleNode* next
    ) {
        if (prev != nullptr) {
            prev->next = next;
        }
        curr->next = nullptr;
    }

    /* Break a linked list at the specified nodes. */
    inline static void split(SingleNode* prev, SingleNode* curr) {
        if (prev != nullptr) {
            prev->next = nullptr;
        }
    }

    /* Join the list at the specified nodes. */
    inline static void join(SingleNode* prev, SingleNode* curr) {
        if (prev != nullptr) {
            prev->next = curr;
        }
    }

    /* Check that the wrapped value is an instance of the specialized class. */
    inline static int typecheck(SingleNode* node, PyObject* specialization) {
        int comp = PyObject_IsInstance(node->value, specialization);
        if (comp == 0) {  // value is not an instance of specialization
            PyErr_Format(
                PyExc_TypeError,
                "%R is not of type %R",
                node->value,
                specialization
            );
        }

        return comp + (comp < 0);  // 0 signals TypeError()
    }

};


/* A doubly-linked list node containing a single PyObject* reference. */
struct DoubleNode {
    PyObject* value;
    DoubleNode* next;
    DoubleNode* prev;

    /* Initialize a newly-allocated node. */
    inline static DoubleNode* init(DoubleNode* node, PyObject* value) {
        Py_INCREF(value);
        node->value = value;
        node->next = nullptr;
        node->prev = nullptr;
        return node;
    }

    /* Initialize a copied node. */
    inline static DoubleNode* init_copy(DoubleNode* new_node, DoubleNode* old_node) {
        Py_INCREF(old_node->value);
        new_node->value = old_node->value;
        new_node->next = nullptr;
        new_node->prev = nullptr;
        return new_node;
    }

    /* Tear down a node before freeing it. */
    inline static void teardown(DoubleNode* node) {
        Py_DECREF(node->value);
    }

    /* Link the node to its neighbors to form a doubly-linked list. */
    inline static void link(
        DoubleNode* prev,
        DoubleNode* curr,
        DoubleNode* next
    ) {
        if (prev != nullptr) {
            prev->next = curr;
        }
        curr->prev = prev;
        curr->next = next;
        if (next != nullptr) {
            next->prev = curr;
        }
    }

    /* Unlink the node from its neighbors. */
    inline static void unlink(
        DoubleNode* prev,
        DoubleNode* curr,
        DoubleNode* next
    ) {
        if (prev != nullptr) {
            prev->next = next;
        }
        if (next != nullptr) {
            next->prev = prev;
        }
    }

    /* Break a linked list at the specified nodes. */
    inline static void split(DoubleNode* prev, DoubleNode* curr) {
        if (prev != nullptr) {
            prev->next = nullptr;
        }
        if (curr != nullptr) {
            curr->prev = nullptr;
        }
    }

    /* Join the list at the specified nodes. */
    inline static void join(DoubleNode* prev, DoubleNode* curr) {
        if (prev != nullptr) {
            prev->next = curr;
        }
        if (curr != nullptr) {
            curr->prev = prev;
        }
    }

    /* Check that the wrapped value is an instance of the specialized class. */
    inline static int typecheck(DoubleNode* node, PyObject* specialization) {
        int comp = PyObject_IsInstance(node->value, specialization);
        if (comp == 0) {  // value is not an instance of specialization
            PyErr_Format(
                PyExc_TypeError,
                "%R is not of type %R",
                node->value,
                specialization
            );
        }

        return comp + (comp < 0);  // 0 signals TypeError()
    }

};


/* A node decorator that computes a key function on a node's underlying value
for use in sorting algorithms. */
template <typename NodeType>
struct Keyed {
    NodeType* node;  // reference to decorated node
    PyObject* value;  // precomputed key
    Keyed<NodeType>* next;

    /* Initialize a newly-allocated node. */
    inline static Keyed<NodeType>* init(
        Keyed<NodeType>* node,
        PyObject* key_value,
        NodeType* wrapped
    ) {
        // NOTE: We mask the node's original value with the precomputed key,
        // allowing us to use the exact same sorting algorithms in both cases.
        Py_INCREF(key_value);
        node->value = key_value;
        node->node = wrapped;
        node->next = nullptr;
        return node;
    }

    // NOTE: we do not provide an init_copy() method because we're storing a
    // direct reference to another node.  We shouldn't ever be copying a
    // Keyed<> node anyways, since they're only meant to be used during sort().
    // This omission just makes it explicit.

    /* Tear down a node before freeing it. */
    inline static void teardown(Keyed<NodeType>* node) {
        Py_DECREF(node->value);  // release reference on precomputed key
    }

    /* Link the node to its neighbors to form a singly-linked list. */
    inline static void link(
        Keyed<NodeType>* prev,
        Keyed<NodeType>* curr,
        Keyed<NodeType>* next
    ) {
        if (prev != nullptr) {
            prev->next = curr;
        }
        curr->next = next;
    }

    /* Unlink the node from its neighbors. */
    inline static void unlink(
        Keyed<NodeType>* prev,
        Keyed<NodeType>* curr,
        Keyed<NodeType>* next
    ) {
        if (prev != nullptr) {
            prev->next = next;
        }
        curr->next = nullptr;
    }

    /* Break a linked list at the specified nodes. */
    inline static void split(Keyed<NodeType>* prev, Keyed<NodeType>* curr) {
        if (prev != nullptr) {
            prev->next = nullptr;
        }
    }

    /* Join the list at the specified nodes. */
    inline static void join(Keyed<NodeType>* prev, Keyed<NodeType>* curr) {
        if (prev != nullptr) {
            prev->next = curr;
        }
    }

    /* Check that the wrapped value is an instance of the specialized class. */
    inline static int typecheck(Keyed<NodeType>* node, PyObject* specialization) {
        int comp = PyObject_IsInstance(node->value, specialization);
        if (comp == 0) {  // value is not an instance of specialization
            PyErr_Format(
                PyExc_TypeError,
                "%R is not of type %R",
                node->value,
                specialization
            );
        }

        // NOTE: we adjust the return value to use this method as a boolean
        // expression in a simple if statement
        return comp + (comp < 0);  // 0 signals TypeError()
    }

};


//////////////////////////
////    DECORATORS    ////
//////////////////////////


/* A node decorator that computes the hash of the underlying PyObject* and
caches it alongside the node's original fields. */
template <typename NodeType>
struct Hashed : public NodeType {
    // NOTE: this is used internally by the SetView class to avoid recomputing
    // a node's hash every time it is requested.  It is automatically applied
    // to the `NodeType` template parameter when a SetView is constructed.
    using Node = Hashed<NodeType>;

    Py_hash_t hash;

    /* Initialize a newly-allocated node. */
    inline static Node* init(Node* node, PyObject* value) {
        node = static_cast<Node*>(NodeType::init(node, value));
        if (node == nullptr) {  // Error during decorated init()
            return nullptr;  // propagate
        }

        // compute hash
        node->hash = PyObject_Hash(value);
        if (node->hash == -1 && PyErr_Occurred()) {
            NodeType::teardown(node);  // free any resources allocated during init()
            return nullptr;  // propagate TypeError()
        }

        // return initialized node
        return node;
    }

    /* Initialize a copied node. */
    inline static Node* init_copy(Node* new_node, Node* old_node) {
        new_node = static_cast<Node*>(NodeType::init_copy(new_node, old_node));
        if (new_node == nullptr) {  // Error during decorated init_copy()
            return nullptr;  // propagate
        }

        // reuse the pre-computed hash
        new_node->hash = old_node->hash;
        return new_node;
    }

};


/* A node decorator that hashes the underlying object and adds a second
PyObject* reference, allowing the list to act as a dictionary. */
template <typename NodeType>
struct Mapped : public NodeType {
    // NOTE: this is used internally by the DictView class to map keys to
    // values during dictionary lookups.  It is automatically applied to the
    // `NodeType` template parameter when a DictView is constructed.
    using Node = Mapped<NodeType>;

    Py_hash_t hash;
    PyObject* mapped;

    /* Initialize a newly-allocated node (1-argument version). */
    inline static Node* init(Node* node, PyObject* value) {
        // Check that item is a tuple of size 2 (key-value pair)
        if (!PyTuple_Check(value) || PyTuple_Size(value) != 2) {
            PyErr_Format(
                PyExc_TypeError,
                "Expected tuple of size 2 (key, value), not: %R",
                value
            );
            return nullptr;  // propagate TypeError()
        }

        // unpack tuple and pass to 2-argument version
        PyObject* key = PyTuple_GetItem(value, 0);
        PyObject* mapped = PyTuple_GetItem(value, 1);
        return init(node, key, mapped);
    }

    /* Initialize a newly-allocated node (2-argument version). */
    inline static Node* init(Node* node, PyObject* value, PyObject* mapped) {
        node = static_cast<Node*>(NodeType::init(node, value));
        if (node == nullptr) {  // Error during decorated init()
            return nullptr;  // propagate
        }

        // compute hash
        node->hash = PyObject_Hash(value);
        if (node->hash == -1 && PyErr_Occurred()) {
            NodeType::teardown(node);  // free any resources allocated during init()
            return nullptr;  // propagate TypeError()
        }

        // store a reference to the mapped value
        Py_INCREF(mapped);
        node->mapped = mapped;

        // return initialized node
        return node;
    }

    /* Initialize a copied node. */
    inline static Node* init_copy(Node* new_node, Node* old_node) {
        new_node = static_cast<Node*>(NodeType::init_copy(new_node, old_node));
        if (new_node == nullptr) {  // Error during decrated init_copy()
            return nullptr;  // propagate
        }

        // reuse the pre-computed hash
        new_node->hash = old_node->hash;

        // store a new reference to mapped value
        Py_INCREF(old_node->mapped);
        new_node->mapped = old_node->mapped;

        // return initialized node
        return new_node;
    }

    /* Tear down a node before freeing it. */
    inline static void teardown(Node* node) {
        Py_DECREF(node->mapped);  // release mapped value
        NodeType::teardown(node);
    }

};


/* A node decorator that adds a frequency count to the underyling node type. */
template <typename NodeType>
struct Counted : public NodeType {
    size_t frequency;

    /* Initialize a newly-allocated node. */
    inline static Counted<NodeType>* init(Counted<NodeType>* node, PyObject* value) {
        node = (Counted<NodeType>*)NodeType::init(node, value);
        if (node == nullptr) {  // Error during decorated init()
            return nullptr;  // propagate
        }

        // initialize frequency
        node->frequency = 1;

        // return initialized node
        return node;
    }

    /* Initialize a copied node. */
    inline static Counted<NodeType>* init_copy(
        Counted<NodeType>* new_node,
        Counted<NodeType>* old_node
    ) {
        // delegate to templated init_copy() method
        new_node = (Counted<NodeType>*)NodeType::init_copy(new_node, old_node);
        if (new_node == nullptr) {  // Error during templated init_copy()
            return nullptr;  // propagate
        }

        // copy frequency
        new_node->frequency = old_node->frequency;
        return new_node;
    }

    /* Tear down a node before freeing it. */
    inline static void teardown(Counted<NodeType>* node) {
        node->frequency = 0; // reset frequency
        NodeType::teardown(node);
    }
};


//////////////////////
////    TRAITS    ////
//////////////////////


/* A trait that detects whether the templated node type is doubly-linked (i.e.
has a `prev` pointer). */
template <typename Node>
struct is_doubly_linked : std::integral_constant<
    bool,
    std::is_base_of<DoubleNode, Node>::value
    // || std::is_base_of<OtherNode, Node>::value  // additional node types
> {};


//////////////////////////
////    ALLOCATORS    ////
//////////////////////////


/* Common interface for all node allocators. */
template <typename Node>
class BaseAllocator {
protected:
    size_t alive;  // number of currently-alive nodes (for memory usage/debugging)

    /* A wrapper around malloc() that can help catch memory leaks. */
    inline static Node* malloc_node(PyObject* value) {
        // print allocation/deallocation messages if DEBUG=TRUE
        if constexpr (DEBUG) {
            printf("    -> malloc: %s\n", repr(value));
        }

        // malloc()
        return static_cast<Node*>(malloc(sizeof(Node)));  // may be NULL
    }

    /* A wrapper around free() that can help catch memory leaks. */
    inline static void free_node(Node* node) {
        // print allocation/deallocation messages if DEBUG=TRUE
        if constexpr (DEBUG) {
            printf("    -> free: %s\n", repr(node->value));
        }

        // free()
        free(node);
    }

public:

    BaseAllocator() : alive(0) {};

    template <typename... Args>
    Node* create(Args... args);
    Node* copy(Node* node);
    void recycle(Node* node);

    /* Return the number of currently-allocated nodes. */
    inline size_t allocated() {
        return this->alive;
    };

    /* Return the total amount of memory being managed by this allocator. */
    inline size_t nbytes() {
        return this->alive * sizeof(Node) + sizeof(*this);
    };

};


/* An allocator that directly allocates and frees each node. */
template <typename Node>
class DirectAllocator : public BaseAllocator<Node> {
public:

    /* Initialize a new node for the list. */
    template <typename... Args>
    inline Node* create(PyObject* value, Args... args) {
        // allocate a blank node
        Node* node = BaseAllocator<Node>::malloc_node(value);
        if (node == nullptr) {  // malloc() failed
            PyErr_NoMemory();
            return nullptr;  // propagate
        }

        // variadic dispatch to one of the node's init() methods
        Node* initialized = Node::init(node, value, args...);
        if (initialized == nullptr) {  // Error during init()
            BaseAllocator<Node>::free_node(node);
        }

        // return initialized node
        this->alive++;
        return initialized;
    }

    /* Copy an existing node. */
    inline Node* copy(Node* node) {
        Node* copied = BaseAllocator<Node>::malloc_node(node->value);
        if (copied == nullptr) {  // malloc() failed
            PyErr_NoMemory();
            return nullptr;  // propagate
        }

        // dispatch to node's init_copy() method
        Node* initialized = Node::init_copy(copied, node);
        if (initialized == nullptr) {  // Error during init_copy()
            BaseAllocator<Node>::free_node(copied);
        }

        // return initialized node
        this->alive++;
        return initialized;
    }

    /* Free a node, returning its resources to the allocator. */
    inline void recycle(Node* node) {
        Node::teardown(node);
        BaseAllocator<Node>::free_node(node);
        this->alive--;
    }

};


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
            node = BaseAllocator<Node>::malloc_node(value);  // malloc()
        }
        return node;  // may be NULL
    }

    /* Push a node to the freelist or free it directly. */
    inline void push_node(Node* node) {
        if (freelist.size() < max_size) {
            freelist.push(node);  // push to freelist
        } else {
            BaseAllocator<Node>::free_node(node);  // free()
        }
    }

public:
    size_t max_size;

    /* Create a freelist of the given size */
    FreeListAllocator(size_t freelist_size) : BaseAllocator<Node>() {
        max_size = freelist_size;
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
        this->alive++;
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
            BaseAllocator<Node>::free_node(node);
        }
    }

    /* Get the number of nodes stored in the freelist. */
    inline size_t size() const {
        return freelist.size();
    }

};






#endif // NODE_H include guard
