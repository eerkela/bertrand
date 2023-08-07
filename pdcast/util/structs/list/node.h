// include guard prevents multiple inclusion
#ifndef NODE_H
#define NODE_H

#include <queue>  // for std::queue
#include <Python.h>  // for CPython API


// TODO: Keyed<> nodes call initialize() directly with a single PyObject* value
// argument, but this fails for Mapped<>, which expects two arguments.


// TODO: Keyed<> should probably just be placed here rather than in sort.h


/////////////////////////
////    CONSTANTS    ////
/////////////////////////


/* For efficient memory management, every View maintains its own freelist of
deallocated nodes that can be reused for fast allocation. */
const unsigned int FREELIST_SIZE = 32;


/////////////////////
////    NODES    ////
/////////////////////


struct SingleNode {
    PyObject* value;
    SingleNode* next;

    /* Freelist constructor. */
    inline static SingleNode* allocate(
        std::queue<SingleNode*>& freelist,
        PyObject* value
    ) {
        SingleNode* node;

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            node = (SingleNode*)malloc(sizeof(SingleNode));
            if (node == NULL) {
                throw std::bad_alloc();
            }
        } else {
            node = freelist.front();
            freelist.pop();
        }

        initialize(node, value);
        return node;
    }

    /* Initialize a newly-allocated node. */
    inline static void initialize(SingleNode* node, PyObject* value) {
        Py_INCREF(value);
        node->value = value;
        node->next = NULL;
    }

    /* Free a node and add it to the freelist. */
    inline static void deallocate(
        std::queue<SingleNode*>& freelist,
        SingleNode* node
    ) {
        Py_DECREF(node->value);
        if (freelist.size() <= FREELIST_SIZE) {
            freelist.push(node);
        } else {
            free(node);
        }
    }

    /* Copy constructor. */
    inline static SingleNode* copy(
        std::queue<SingleNode*>& freelist,
        SingleNode* node
    ) {
        return allocate(freelist, node->value);
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

};


struct DoubleNode {
    PyObject* value;
    DoubleNode* next;
    DoubleNode* prev;

    /* Freelist constructor. */
    inline static DoubleNode* allocate(
        std::queue<DoubleNode*>& freelist,
        PyObject* value
    ) {
        DoubleNode* node;

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            node = (DoubleNode*)malloc(sizeof(DoubleNode));
            if (node == NULL) {
                throw std::bad_alloc();
            }
        } else {
            node = freelist.front();
            freelist.pop();
        }

        initialize(node, value);
        return node;
    }

    /* Initialize a newly-allocated node. */
    inline static void initialize(DoubleNode* node, PyObject* value) {
        Py_INCREF(value);
        node->value = value;
        node->next = NULL;
        node->prev = NULL;
    }

    /* Free a node and add it to the freelist. */
    inline static void deallocate(
        std::queue<DoubleNode*>& freelist,
        DoubleNode* node
    ) {
        Py_DECREF(node->value);
        if (freelist.size() <= FREELIST_SIZE) {
            freelist.push(node);
        } else {
            free(node);
        }
    }

    /* Copy constructor. */
    inline static DoubleNode* copy(
        std::queue<DoubleNode*>& freelist,
        DoubleNode* node
    ) {
        return allocate(freelist, node->value);
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
};


template <typename NodeType>
struct Hashed : public NodeType {
    Py_hash_t hash;

    /* Freelist constructor. */
    inline static Hashed<NodeType>* allocate(
        std::queue<Hashed<NodeType>*>& freelist,
        PyObject* value
    ) {
        Hashed<NodeType>* node;

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            node = (Hashed<NodeType>*)malloc(sizeof(Hashed<NodeType>));
            if (node == NULL) {
                throw std::bad_alloc();
            }
        } else {
            node = freelist.front();
            freelist.pop();
        }

        initialize(node, value);
        if (PyErr_Occurred()) {  // error during hash()
            deallocate(freelist, node);
            return NULL;
        }
        return node;
    }

    /* Initialize a newly-allocated node. */
    inline static void initialize(Hashed<NodeType>* node, PyObject* value) {
        NodeType::initialize(node, value);
        node->hash = PyObject_Hash(value);
    }

    /* Free a node and add it to the freelist. */
    inline static void deallocate(
        std::queue<Hashed<NodeType>*>& freelist,
        Hashed<NodeType>* node
    ) {
        Py_DECREF(node->value);
        if (freelist.size() <= FREELIST_SIZE) {
            freelist.push(node);
        } else {
            free(node);
        }
    }

    /* Copy constructor. */
    inline static Hashed<NodeType>* copy(
        std::queue<Hashed<NodeType>*>& freelist,
        Hashed<NodeType>* node
    ) {
        // reuse the old node's hash value
        Hashed<NodeType>* new_node;

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            new_node = (Hashed<NodeType>*)malloc(sizeof(Hashed<NodeType>));
            if (new_node == NULL) {
                throw std::bad_alloc();
            }
        } else {
            new_node = freelist.front();
            freelist.pop();
        }

        // initialize node
        NodeType::initialize(new_node, node->value);
        new_node->hash = node->hash;  // reuse the pre-computed hash
        return new_node;
    }

};


template <typename NodeType>
struct Mapped : public NodeType {
    Py_hash_t hash;
    PyObject* mapped;

    /* Freelist constructor. */
    inline static Mapped<NodeType>* allocate(
        std::queue<Mapped<NodeType>*>& freelist,
        PyObject* value,
        PyObject* mapped
    ) {
        Mapped<NodeType>* node;

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            node = (Mapped<NodeType>*)malloc(sizeof(Mapped<NodeType>));
            if (node == NULL) {
                throw std::bad_alloc();
            }
        } else {
            node = freelist.front();
            freelist.pop();
        }

        initialize(node, value, mapped);
        if (PyErr_Occurred()) {  // error during hash()
            deallocate(freelist, node);
            return NULL;
        }
        return node;
    }

    /* Initialize a newly-allocated node. */
    inline static void initialize(
        Mapped<NodeType>* node,
        PyObject* value,
        PyObject* mapped
    ) {
        NodeType::initialize(node, value);
        node->hash = PyObject_Hash(value);
        Py_INCREF(mapped);
        node->mapped = mapped;
    }

    /* Free a node and add it to the freelist. */
    inline static void deallocate(
        std::queue<Mapped<NodeType>*>& freelist,
        Mapped<NodeType>* node
    ) {
        Py_DECREF(node->value);
        Py_DECREF(node->mapped);
        if (freelist.size() <= FREELIST_SIZE) {
            freelist.push(node);
        } else {
            free(node);
        }
    }

    /* Copy constructor. */
    inline static Mapped<NodeType>* copy(
        std::queue<Mapped<NodeType>*>& freelist,
        Mapped<NodeType>* node
    ) {
        // reuse the old node's hash value
        Mapped<NodeType>* new_node;

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            new_node = (Mapped<NodeType>*)malloc(sizeof(Mapped<NodeType>));
            if (new_node == NULL) {
                throw std::bad_alloc();
            }
        } else {
            new_node = freelist.front();
            freelist.pop();
        }

        // initialize node
        NodeType::initialize(new_node, node->value);
        new_node->hash = node->hash;  // reuse the pre-computed hash
        new_node->mapped = node->mapped;  // copy mapped value
        Py_INCREF(node->mapped);
        return new_node;
    }

};


#endif // NODE_H include guard
