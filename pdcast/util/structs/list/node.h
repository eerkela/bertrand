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


/* DEBUG=TRUE adds print statements for every node allocation/deallocation to
help catch memory leaks. */
const bool DEBUG = true;


/* Every View maintains a freelist of blank nodes that can be reused for fast
allocation/deallocation. */
const unsigned int FREELIST_SIZE = 32;  // max size of each View's freelist


//////////////////////////
////    ALLOCATORS    ////
//////////////////////////


/* A factory for the templated node that uses a freelist to speed up allocation. */
template <typename Derived>
struct Allocater {
private:

    /* A wrapper around malloc() that can help catch memory leaks. */
    inline static Derived* malloc_node(PyObject* value) {
        // print allocation/deallocation messages if DEBUG=TRUE
        if (DEBUG) {
            PyObject* python_repr = PyObject_Repr(value);
            const char* c_repr = PyUnicode_AsUTF8(python_repr);
            Py_DECREF(python_repr);
            printf("    -> malloc: %s\n", c_repr);
        }

        // malloc() node
        Derived* node = (Derived*)malloc(sizeof(Derived));
        if (node == NULL) {
            throw std::bad_alloc();
        }
        return node;
    }

    /* A wrapper around free() that can help catch memory leaks. */
    inline static void free_node(Derived* node) {
        // print allocation/deallocation messages if DEBUG=TRUE
        if (DEBUG) {
            PyObject* python_repr = PyObject_Repr(node->value);
            const char* c_repr = PyUnicode_AsUTF8(python_repr);
            Py_DECREF(python_repr);
            printf("    -> free: %s\n", c_repr);
        }

        // free() node
        free(node);
    }

    /* Pop a node from the freelist or allocate a new one directly. */
    inline static Derived* pop(std::queue<Derived*>& freelist, PyObject* value) {
        Derived* node;
        if (!freelist.empty()) {
            node = freelist.front();  // pop from freelist
            freelist.pop();
        } else {
            node = malloc_node(value);  // allocate new node
        }
        return node;
    }

    /* Push a node to the freelist or free it directly. */
    inline static void push(std::queue<Derived*>& freelist, Derived* node) {
        if (freelist.size() < FREELIST_SIZE) {
            freelist.push(node);
        } else {
            free_node(node);
        }
    }

public:

    /* Allocate a new node for the specified value. */
    inline static Derived* create(std::queue<Derived*>& freelist, PyObject* value) {
        Derived* node = pop(freelist, value);
        node = Derived::init(node, value);  // 1-argument init()
        if (node == NULL) {
            push(freelist, node);
        }
        return node;
    }

    /* Allocate a new node for the specified key-value pair. */
    inline static Derived* create(
        std::queue<Derived*>& freelist,
        PyObject* value,
        PyObject* mapped
    ) {
        Derived* node = pop(freelist, value);
        node = Derived::init(node, value, mapped);  // 2-argument init()
        if (node == NULL) {
            push(freelist, node);
        }
        return node;
    }

    /* Allocate a copy of an existing node. */
    inline static Derived* copy(std::queue<Derived*>& freelist, Derived* old_node) {
        Derived* new_node = pop(freelist, old_node->value);
        new_node = Derived::init_copy(new_node, old_node);
        if (new_node == NULL) {  // error during init_copy()
            push(freelist, new_node);
        }
        return new_node;
    }

    /* Release a node, freeing its resources and pushing it to the freelist. */
    inline static void recycle(std::queue<Derived*>& freelist, Derived* node) {
        Derived::teardown(node);  // release allocated resources
        push(freelist, node);
    }

    /* Clear a list from head to tail, recycling all of the contained nodes. */
    inline static void recycle_list(std::queue<Derived*>& freelist, Derived* head) {
        Derived* next;
        while (head != NULL) {
            next = (Derived*)head->next;
            recycle(freelist, head);
            head = next;
        }
    }

    /* Delete a node, freeing its resources without adding it to the freelist. */
    inline static void discard(Derived* node) {
        Derived::teardown(node);  // release allocated resources
        free_node(node);
    }

    /* Clear a list from head to tail, discarding all of the contained nodes. */
    inline static void discard_list(Derived* head) {
        Derived* next;
        while (head != NULL) {
            next = (Derived*)head->next;
            discard(head);
            head = next;
        }
    }

    /* Clear a freelist, discarding all of its stored nodes. */
    inline static void discard_freelist(std::queue<Derived*>& freelist) {
        Derived* node;
        while (!freelist.empty()) {
            node = freelist.front();
            freelist.pop();
            free_node(node);  // no teardown() necessary
        }
    }

};


/////////////////////
////    NODES    ////
/////////////////////


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

};


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
};


template <typename NodeType>
struct Hashed : public NodeType {
    Py_hash_t hash;

    /* Initialize a newly-allocated node. */
    inline static Hashed<NodeType>* init(Hashed<NodeType>* node, PyObject* value) {
        // delegate to templated init() method
        node = (Hashed<NodeType>*)NodeType::init(node, value);
        if (node == NULL) {  // Error during templated init()
            return NULL;
        }

        // compute hash
        node->hash = PyObject_Hash(value);
        if (node->hash == -1 && PyErr_Occurred()) {  // TypeError() during hash()
            NodeType::teardown(node);  // free any resources allocated during init()
            return NULL;
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
            return NULL;
        }

        // reuse the pre-computed hash
        new_node->hash = old_node->hash;
        return new_node;
    }

};


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
            return NULL;
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
            return NULL;
        }

        // compute hash
        node->hash = PyObject_Hash(value);
        if (node->hash == -1 && PyErr_Occurred()) {  // TypeError() during hash()
            NodeType::teardown(node);  // free any resources allocated during init()
            return NULL;
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
            return NULL;
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


#endif // NODE_H include guard
