// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_NODE_H
#define BERTRAND_STRUCTS_CORE_NODE_H

#include <queue>  // for std::queue
#include <stdexcept>  // for std::invalid_argument
#include <type_traits>  // for std::integral_constant, std::is_base_of_v
#include <Python.h>  // for CPython API


//////////////////////////
////    BASE NODES    ////
//////////////////////////


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
    // NOTE: this is a special case of node used in the `sort()` method to
    // apply a key function to each node's value.  It is not meant to be used
    // in any other context.

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


///////////////////////////////
////    NODE DECORATORS    ////
//////////////////////////////


/* A node decorator that computes the hash of the underlying PyObject* and
caches it alongside the node's original fields. */
template <typename NodeType>
struct Hashed : public NodeType {
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


///////////////////////////
////    NODE TRAITS    ////
///////////////////////////


/* A trait that detects whether the templated node type is doubly-linked (i.e.
has a `prev` pointer). */
template <typename Node>
struct is_doubly_linked : std::integral_constant<
    bool,
    std::is_base_of_v<DoubleNode, Node>
    // || std::is_base_of<OtherNode, Node>::value  // additional node types
> {};


#endif // BERTRAND_STRUCTS_CORE_NODE_H include guard
