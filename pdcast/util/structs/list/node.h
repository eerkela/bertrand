// include guard prevents multiple inclusion
#ifndef NODE_H
#define NODE_H

#include <queue>  // for std::queue
#include <Python.h>  // for CPython API


/////////////////////////
////    CONSTANTS    ////
/////////////////////////


/* DEBUG=TRUE adds print statements for every node allocation/deallocation to
help catch memory leaks. */
const bool DEBUG = false;


/* Every View maintains a freelist of blank nodes that can be reused for fast
allocation/deallocation. */
const unsigned int FREELIST_SIZE = 32;  // max size of each View's freelist


/////////////////////////
////    FUNCTIONS    ////
/////////////////////////


/* Get the Python repr() of an arbitrary PyObject* as a C string. */
const char* repr(PyObject* obj) {
    if (obj == NULL) {
        return "NULL";
    }

    // call repr()
    PyObject* py_repr = PyObject_Repr(obj);
    if (py_repr == NULL) {
        return "NULL";
    }

    // convert to UTF-8
    const char* c_repr = PyUnicode_AsUTF8(py_repr);
    if (c_repr == NULL) {
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
        node->next = NULL;
        return node;
    }

    /* Initialize a copied node. */
    inline static SingleNode* init_copy(SingleNode* new_node, SingleNode* old_node) {
        Py_INCREF(old_node->value);
        new_node->value = old_node->value;
        new_node->next = NULL;
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
        if (prev != NULL) {
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
        if (prev != NULL) {
            prev->next = next;
        }
        curr->next = NULL;
    }

    /* Break a linked list at the specified nodes. */
    inline static void split(SingleNode* prev, SingleNode* curr) {
        if (prev != NULL) {
            prev->next = NULL;
        }
    }

    /* Join the list at the specified nodes. */
    inline static void join(SingleNode* prev, SingleNode* curr) {
        if (prev != NULL) {
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
        node->next = NULL;
        node->prev = NULL;
        return node;
    }

    /* Initialize a copied node. */
    inline static DoubleNode* init_copy(DoubleNode* new_node, DoubleNode* old_node) {
        Py_INCREF(old_node->value);
        new_node->value = old_node->value;
        new_node->next = NULL;
        new_node->prev = NULL;
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
        if (prev != NULL) {
            prev->next = curr;
        }
        curr->prev = prev;
        curr->next = next;
        if (next != NULL) {
            next->prev = curr;
        }
    }

    /* Unlink the node from its neighbors. */
    inline static void unlink(
        DoubleNode* prev,
        DoubleNode* curr,
        DoubleNode* next
    ) {
        if (prev != NULL) {
            prev->next = next;
        }
        if (next != NULL) {
            next->prev = prev;
        }
    }

    /* Break a linked list at the specified nodes. */
    inline static void split(DoubleNode* prev, DoubleNode* curr) {
        if (prev != NULL) {
            prev->next = NULL;
        }
        if (curr != NULL) {
            curr->prev = NULL;
        }
    }

    /* Join the list at the specified nodes. */
    inline static void join(DoubleNode* prev, DoubleNode* curr) {
        if (prev != NULL) {
            prev->next = curr;
        }
        if (curr != NULL) {
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


/* A node decorator that computes the hash of the Python object and caches it
alongside the node's original fields. */
template <typename NodeType>
struct Hashed : public NodeType {
    Py_hash_t hash;

    /* Initialize a newly-allocated node. */
    inline static Hashed<NodeType>* init(Hashed<NodeType>* node, PyObject* value) {
        // delegate to templated init() method
        node = (Hashed<NodeType>*)NodeType::init(node, value);
        if (node == NULL) {  // Error during templated init()
            return NULL;  // propagate
        }

        // compute hash
        node->hash = PyObject_Hash(value);
        if (node->hash == -1 && PyErr_Occurred()) {
            NodeType::teardown(node);  // free any resources allocated during init()
            return NULL;  // propagate TypeError()
        }

        // return initialized node
        return node;
    }

    /* Initialize a copied node. */
    inline static Hashed<NodeType>* init_copy(
        Hashed<NodeType>* new_node,
        Hashed<NodeType>* old_node
    ) {
        // delegate to templated init_copy() method
        new_node = (Hashed<NodeType>*)NodeType::init_copy(new_node, old_node);
        if (new_node == NULL) {  // Error during templated init_copy()
            return NULL;  // propagate
        }

        // reuse the pre-computed hash
        new_node->hash = old_node->hash;
        return new_node;
    }

};


/* A special case of Hashed<NodeType> that adds a second PyObject* reference,
allowing the list to act as a dictionary. */
template <typename NodeType>
struct Mapped : public NodeType {
    Py_hash_t hash;
    PyObject* mapped;

    /* Initialize a newly-allocated node (1-argument version). */
    inline static Mapped<NodeType>* init(Mapped<NodeType>* node, PyObject* value) {
        // Check that item is a tuple of size 2 (key-value pair)
        if (!PyTuple_Check(value) || PyTuple_Size(value) != 2) {
            PyErr_Format(
                PyExc_TypeError,
                "Expected tuple of size 2 (key, value), not: %R",
                value
            );
            return NULL;  // propagate TypeError()
        }

        // unpack tuple and pass to 2-argument version
        PyObject* key = PyTuple_GetItem(value, 0);
        PyObject* mapped = PyTuple_GetItem(value, 1);
        return init(node, key, mapped);
    }

    /* Initialize a newly-allocated node (2-argument version). */
    inline static Mapped<NodeType>* init(
        Mapped<NodeType>* node,
        PyObject* value,
        PyObject* mapped
    ) {
        // delegate to templated init() method
        node = (Mapped<NodeType>*)NodeType::init(node, value);
        if (node == NULL) {  // Error during templated init()
            return NULL;  // propagate
        }

        // compute hash
        node->hash = PyObject_Hash(value);
        if (node->hash == -1 && PyErr_Occurred()) {
            NodeType::teardown(node);  // free any resources allocated during init()
            return NULL;  // propagate TypeError()
        }

        // store a reference to the mapped value
        Py_INCREF(mapped);
        node->mapped = mapped;

        // return initialized node
        return node;
    }

    /* Initialize a copied node. */
    inline static Mapped<NodeType>* init_copy(
        Mapped<NodeType>* new_node,
        Mapped<NodeType>* old_node
    ) {
        // delegate to templated init_copy() method
        new_node = (Mapped<NodeType>*)NodeType::init_copy(new_node, old_node);
        if (new_node == NULL) {  // Error during templated init_copy()
            return NULL;  // propagate
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
    inline static void teardown(Mapped<NodeType>* node) {
        Py_DECREF(node->mapped);  // release mapped value
        NodeType::teardown(node);
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
        // allowing us to use the exact same algorithm in both cases.
        Py_INCREF(key_value);
        node->value = key_value;
        node->node = wrapped;
        node->next = NULL;
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
        if (prev != NULL) {
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
        if (prev != NULL) {
            prev->next = next;
        }
        curr->next = NULL;
    }

    /* Break a linked list at the specified nodes. */
    inline static void split(Keyed<NodeType>* prev, Keyed<NodeType>* curr) {
        if (prev != NULL) {
            prev->next = NULL;
        }
    }

    /* Join the list at the specified nodes. */
    inline static void join(Keyed<NodeType>* prev, Keyed<NodeType>* curr) {
        if (prev != NULL) {
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

        return comp + (comp < 0);  // 0 signals TypeError()
    }

};


//////////////////////
////    TRAITS    ////
//////////////////////


// We can make algorithm decisions based on whether the underlying node is
// singly- or doubly-linked.


template <typename Node>
struct is_doubly_linked {
    static constexpr bool value = false;  // defaults False
};


template <>
struct is_doubly_linked<DoubleNode> {
    static constexpr bool value = true;
};


template <>
struct is_doubly_linked<Hashed<DoubleNode>> {
    static constexpr bool value = true;
};


template <>
struct is_doubly_linked<Mapped<DoubleNode>> {
    static constexpr bool value = true;
};


//////////////////////////
////    ALLOCATORS    ////
//////////////////////////


/* A factory for the templated node that uses a freelist to speed up allocation. */
template <typename Node>
struct Allocater {
private:

    /* A wrapper around malloc() that can help catch memory leaks. */
    inline static Node* malloc_node(PyObject* value) {
        // print allocation/deallocation messages if DEBUG=TRUE
        if (DEBUG) {
            printf("    -> malloc: %s\n", repr(value));
        }

        // malloc()
        return (Node*)malloc(sizeof(Node));  // may be NULL
    }

    /* A wrapper around free() that can help catch memory leaks. */
    inline static void free_node(Node* node) {
        // print allocation/deallocation messages if DEBUG=TRUE
        if (DEBUG) {
            printf("    -> free: %s\n", repr(node->value));
        }

        // free()
        free(node);
    }

    /* Pop a node from the freelist or allocate a new one directly. */
    inline static Node* pop(std::queue<Node*>& freelist, PyObject* value) {
        Node* node;
        if (!freelist.empty()) {
            node = freelist.front();  // pop from freelist
            freelist.pop();
        } else {
            node = malloc_node(value);  // allocate new node
        }
        return node;  // may be NULL if malloc() failed
    }

    /* Push a node to the freelist or free it directly. */
    inline static void push(std::queue<Node*>& freelist, Node* node) {
        if (freelist.size() < FREELIST_SIZE) {
            freelist.push(node);
        } else {
            free_node(node);
        }
    }

public:

    /* Allocate a new node for the specified value. */
    template <typename... Args>
    inline static Node* create(
        std::queue<Node*>& freelist,
        PyObject* value,
        Args... args
    ) {
        // get blank node
        Node* node = pop(freelist, value);
        if (node == NULL) {  // malloc() failed
            PyErr_NoMemory();  // set MemoryError
            return NULL;
        }

        // NOTE: we select one of the init() methods defined on the derived
        // class, which can be arbitrarily specialized.
        node = Node::init(node, value, args...);  // variadic init()
        if (node == NULL) {  // error during dispatched init()
            push(freelist, node);
        }

        // return initialized node
        return node;
    }

    /* Allocate a copy of an existing node. */
    inline static Node* copy(std::queue<Node*>& freelist, Node* old_node) {
        // get blank node
        Node* new_node = pop(freelist, old_node->value);
        if (new_node == NULL) {  // malloc() failed
            PyErr_NoMemory();  // set MemoryError
            return NULL;
        }

        // initialize according to template parameter
        new_node = Node::init_copy(new_node, old_node);
        if (new_node == NULL) {  // error during init_copy()
            push(freelist, new_node);  // push allocated node to freelist
        }

        // return initialized node
        return new_node;
    }

    /* Release a node, freeing its resources and pushing it to the freelist. */
    inline static void recycle(std::queue<Node*>& freelist, Node* node) {
        Node::teardown(node);  // release allocated resources
        push(freelist, node);
    }

    /* Clear a list from head to tail, recycling all of the contained nodes. */
    inline static void recycle_list(std::queue<Node*>& freelist, Node* head) {
        Node* next;
        while (head != NULL) {
            next = (Node*)head->next;
            recycle(freelist, head);
            head = next;
        }
    }

    /* Delete a node, freeing its resources without adding it to the freelist. */
    inline static void discard(Node* node) {
        Node::teardown(node);  // release allocated resources
        free_node(node);
    }

    /* Clear a list from head to tail, discarding all of the contained nodes. */
    inline static void discard_list(Node* head) {
        Node* next;
        while (head != NULL) {
            next = (Node*)head->next;
            discard(head);
            head = next;
        }
    }

    /* Clear a freelist, discarding all of its stored nodes. */
    inline static void discard_freelist(std::queue<Node*>& freelist) {
        Node* node;
        while (!freelist.empty()) {
            node = freelist.front();
            freelist.pop();
            free_node(node);  // no teardown() necessary
        }
    }

};


#endif // NODE_H include guard
