// include guard prevents multiple inclusion
#ifndef NODE_H
#define NODE_H

#include <queue>  // for std::queue
#include <type_traits>  // for std::integer_constant, std::is_base_of
#include <Python.h>  // for CPython API


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


/* A factory for the templated node that uses a freelist to speed up allocation. */
template <typename Node>
struct Allocater {
private:

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
        if (node == nullptr) {  // malloc() failed
            PyErr_NoMemory();  // set MemoryError
            return nullptr;
        }

        // NOTE: we select one of the init() methods defined on the derived
        // class, which can be arbitrarily specialized.
        node = Node::init(node, value, args...);  // variadic init()
        if (node == nullptr) {  // error during dispatched init()
            push(freelist, node);
        }

        // return initialized node
        return node;
    }

    /* Allocate a copy of an existing node. */
    inline static Node* copy(std::queue<Node*>& freelist, Node* old_node) {
        // get blank node
        Node* new_node = pop(freelist, old_node->value);
        if (new_node == nullptr) {  // malloc() failed
            PyErr_NoMemory();  // set MemoryError
            return nullptr;
        }

        // initialize according to template parameter
        new_node = Node::init_copy(new_node, old_node);
        if (new_node == nullptr) {  // error during init_copy()
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
        while (head != nullptr) {
            next = static_cast<Node*>(head->next);
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
        while (head != nullptr) {
            next = static_cast<Node*>(head->next);
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
