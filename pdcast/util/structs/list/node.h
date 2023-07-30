// include guard prevents multiple inclusion
#ifndef NODE_H
#define NODE_H

#include <cstddef>  // for size_t
#include <utility>  // for std::pair
#include <queue>  // for std::queue
#include <Python.h>  // for CPython API


/////////////////////////
////    CONSTANTS    ////
/////////////////////////


/*DEBUG = TRUE adds print statements for memory allocation/deallocation to help
identify memory leaks.*/
const bool DEBUG = true;


/* For efficient memory management, every ListView maintains its own freelist
of deallocated nodes, which can be reused for repeated allocation.*/
const unsigned int FREELIST_SIZE = 16;


/////////////////////
////    NODES    ////
/////////////////////


// TODO: no need for anything more than SingleNode, DoubleNode, hashed equivalents
// Views add the hash table + freelist + operations.


struct Node {
    PyObject* value;

    /*Hash the underlying value.*/
    inline static Py_hash_t hash(Node node) {
        // C API equivalent of the hash() function
        return PyObject_Hash(value);
    }

}


struct SingleNode : public Node {
    SingleNode* next;

    /*Freelist constructor.*/
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

        // initialize node
        Py_INCREF(value);
        node->value = value;
        node->next = NULL;
        return node;
    }

    /*Freelist destructor.*/
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

    /*Copy constructor.*/
    inline static SingleNode* copy(
        std::queue<SingleNode*>& freelist,
        SingleNode* node
    ) {
        return allocate(freelist, node->value);
    }

    /*Link the node to its neighbors to form a singly-linked list.*/
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

    /*Unlink the node from its neighbors.*/
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

};


struct DoubleNode : public Node {
    DoubleNode* next;
    DoubleNode* prev;

    /*static freelist constructor.*/
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

        // initialize node
        Py_INCREF(value);
        node->value = value;
        node->next = NULL;
        node->prev = NULL;
        return node;
    }

    /*static freelist destructor.*/
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

    /*copy constructor.*/
    inline static DoubleNode* copy(
        std::queue<DoubleNode*>& freelist,
        DoubleNode* node
    ) {
        return allocate(freelist, node->value);
    }

    /*Link the node to its neighbors to form a doubly-linked list.*/
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

    /*Unlink the node from its neighbors.*/
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
};


struct Hashed {
    Py_hash_t _hash;

    /*Return the pre-computed hash.*/
    inline static Py_hash_t hash(HashNode* node) {
        return node->_hash;
    }
}


struct HashedSingleNode : public Hashed, public SingleNode {
    /*Freelist constructor.*/
    inline static HashedSingleNode* allocate(
        std::queue<HashedSingleNode*>& freelist,
        PyObject* value
    ) {
        // C API equivalent of the hash() function
        Py_hash_t hash = PyObject_Hash(value);
        if (hash == -1 && PyErr_Occurred()) {
            return NULL;
        }

        HashedSingleNode* node;

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            node = (HashedSingleNode*)malloc(sizeof(HashedSingleNode));
            if (node == NULL) {
                throw std::bad_alloc();
            }
        } else {
            node = freelist.front();
            freelist.pop();
        }

        // initialize node
        Py_INCREF(value);
        node->value = value;
        node->hash = hash;
        node->next = NULL;
        return node;
    }

    /*Copy constructor.*/
    inline static HashedSingleNode* copy(
        std::queue<HashedSingleNode*>& freelist,
        HashedSingleNode* node
    ) {
        HashedSingleNode* new_node;

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            new_node = (HashedSingleNode*)malloc(sizeof(HashedSingleNode));
            if (new_node == NULL) {
                throw std::bad_alloc();
            }
        } else {
            new_node = freelist.front();
            freelist.pop();
        }

        // initialize node
        Py_INCREF(node->value);
        new_node->value = node->value;
        new_node->hash = node->hash;  // reuse the old node's hash value
        new_node->next = NULL;
        return node;
    }

};


struct HashedDoubleNode : public Hashed, public DoubleNode {
    /*static freelist constructor.*/
    inline static HashedDoubleNode* allocate(
        std::queue<HashedDoubleNode*>& freelist,
        PyObject* value
    ) {        
        // C API equivalent of the hash() function
        Py_hash_t hash = PyObject_Hash(value);
        if (hash == -1 && PyErr_Occurred()) {
            return NULL;
        }

        HashedDoubleNode* node;

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            node = (HashedDoubleNode*)malloc(sizeof(HashedDoubleNode));
            if (node == NULL) {
                throw std::bad_alloc();
            }
        } else {
            node = freelist.front();
            freelist.pop();
        }

        // initialize node
        Py_INCREF(value);
        node->value = value;
        node->hash = hash;
        node->next = NULL;
        node->prev = NULL;
        return node;
    }

    /*copy constructor.*/
    inline static HashedDoubleNode* copy(
        std::queue<HashedDoubleNode*>& freelist,
        HashedDoubleNode* node
    ) {
        // reuse the old node's hash value
        HashedDoubleNode* new_node;

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            new_node = (HashedDoubleNode*)malloc(sizeof(HashedDoubleNode));
            if (new_node == NULL) {
                throw std::bad_alloc();
            }
        } else {
            new_node = freelist.front();
            freelist.pop();
        }

        // initialize node
        Py_INCREF(node->value);
        new_node->value = node->value;
        new_node->hash = node->hash;
        new_node->next = NULL;
        new_node->prev = NULL;
        return new_node;
    }

};


/////////////////////
////    VIEWS    ////
/////////////////////


template <typename T>
class ListView {
private:
    std::queue<T*> freelist;

    /*Allow Python-style negative indexing with wraparound.*/
    inline size_t normalize_index(long long index) {
        // wraparound
        if (index < 0) {
            index += size;
        }

        // boundscheck
        if (index < 0 || index >= (long long)size) {
            throw std::out_of_range("list index out of range");
        }

        return (size_t)index;
    }

public:
    T* head;
    T* tail;
    size_t size;

    /*Construct an empty ListView.*/
    ListView() {
        head = NULL;
        tail = NULL;
        size = 0;
        freelist = std::queue<T*>();
    }

    /*Destroy a ListView and free all its nodes.*/
    ~ListView() {
        T* curr = head;
        T* next;
        PyObject* python_repr;
        const char* c_repr;

        // free all nodes
        while (curr != NULL) {
            // print deallocation message if DEBUG=TRUE
            if (DEBUG) {
                python_repr = PyObject_Repr(curr->value);
                c_repr = PyUnicode_AsUTF8(python_repr);
                Py_DECREF(python_repr);
                printf("    -> free: %s\n", c_repr);
            }

            // destroy node
            next = curr->next;
            Py_DECREF(curr->value);
            free(curr);
            curr = next;
        }
    }

    /*Allocate a new node for the list.*/
    inline T* allocate(PyObject* value) {
        PyObject* python_repr;
        const char* c_repr;

        // print allocation message if DEBUG=TRUE
        if (DEBUG) {
            python_repr = PyObject_Repr(value);
            c_repr = PyUnicode_AsUTF8(python_repr);
            Py_DECREF(python_repr);
            printf("    -> malloc: %s\n", c_repr);
        }

        // delegate to node-specific allocator
        return T::allocate(freelist, value);
    }

    /*Free a node.*/
    inline void deallocate(T* node) {
        PyObject* python_repr;
        const char* c_repr;

        // print deallocation message if DEBUG=TRUE
        if (DEBUG) {
            python_repr = PyObject_Repr(node->value);
            c_repr = PyUnicode_AsUTF8(python_repr);
            Py_DECREF(python_repr);
            printf("    -> free: %s\n", c_repr);
        }

        // delegate to node-specific deallocater
        T::deallocate(freelist, node);
    }

    /*Return the size of the freelist.*/
    inline unsigned char freelist_size() {
        return freelist.size();
    }

    /*Link a node to its neighbors to form a linked list.*/
    inline void link(T* prev, T* curr, T* next) {
        T::link(prev, curr, next);
        if (prev == NULL) {
            head = curr;
        }
        if (next == NULL) {
            tail = curr;
        }
        size++;
    }

    /*Unlink a node from its neighbors.*/
    inline void unlink(T* prev, T* curr, T* next) {
        T::unlink(prev, curr, next);
        if (prev == NULL) {
            head = next;
        }
        if (next == NULL) {
            tail = prev;
        }
        size--;
    }

    /*Clear the list.*/
    inline void clear() {
        T* curr = head;
        T* next;
        while (curr != NULL) {
            next = curr->next;
            deallocate(curr);
            curr = next;
        }
        head = NULL;
        tail = NULL;
        size = 0;
    }

    /*Make a shallow copy of the list.*/
    inline ListView<T>* copy() {
        ListView<T>* copied = new ListView<T>();
        T* old_node = head;
        T* new_node = NULL;
        T* prev = NULL;

        // copy each node in list
        while (old_node != NULL) {
            new_node = T::copy(freelist, old_node);
            T::link(prev, new_node, NULL);
            if (copied->head == NULL) {
                copied->head = new_node;
            }
            prev = new_node;
            old_node = old_node->next;
        }

        copied->tail = new_node;  // last node in copied list
        copied->size = size;  // reuse old size
        return copied;
    }

    // TODO: all of these should be implemented in the Cython wrappers
    // directly.  The ListView is just a stripped down core of the list, whose
    // public interface is completely defined by Cython.



    // /*Append an item to the end of the list.*/
    // inline void append(PyObject* item) {
    //     link(tail, allocate(item), NULL);
    // }

    // /*Append an item to the beginning of the list.*/
    // inline void appendleft(PyObject* item) {
    //     link(NULL, allocate(item), head);
    // }

    /*Stage a new View of nodes to be added to the list.*/
    static ListView<T>* stage(PyObject* iterable, bool reverse = false) {
        // C API equivalent of iter(iterable)
        PyObject* iterator = PyObject_GetIter(iterable);
        if (iterator == NULL) {
            return NULL;
        }

        ListView<T>* staged = new ListView<T>();

        T* node;
        PyObject* item;

        while (true) {
            // C API equivalent of next(iterator)
            item = PyIter_Next(iterator);
            if (item == NULL) { // end of iterator or error
                if (PyErr_Occurred()) {  // error during next()
                    Py_DECREF(item);
                    Py_DECREF(iterator);
                    while (staged->head != NULL) {  // clean up staged nodes
                        node = staged->head;
                        staged->head = staged->head->next;
                        Py_DECREF(node->value);
                        free(node);
                    }
                    return NULL;  // raise exception
                }
                break;
            }

            // allocate a new node
            node = staged->allocate(item);

            // link the node to the staged list
            if (reverse) {
                staged->link(NULL, node, staged->head);
            } else {
                staged->link(staged->tail, node, NULL);
            }

            // advance to next item
            Py_DECREF(item);
        }

        // release reference on iterator
        Py_DECREF(iterator);

        // return the staged View
        return staged;
    }

    // /*Extend the list with a sequence of items.*/
    // void extend(PyObject* iterable) {
    //     ListView<T>* staged = stage(iterable);
    //     if (staged == NULL) {
    //         return;  // raise exception
    //     }

    //     // trivial case: empty iterable
    //     if (staged->head == NULL) {
    //         return;
    //     }

    //     // link the staged nodes to the list
    //     T::link(tail, staged->head, staged->head->next);
    //     if (head == NULL) {
    //         head = staged->head;
    //     }
    //     tail = staged->tail;
    //     size += staged->size;
    // }

    // /*Extend the list to the left.*/
    // void extendleft(PyObject* iterable) {
    //     ListView<T>* staged = stage(iterable, true);  // reverse order
    //     if (staged == NULL) {
    //         return;  // raise exception
    //     }

    //     // trivial case: empty iterable
    //     if (staged->head == NULL) {
    //         return;
    //     }

    //     // link the staged nodes to the list
    //     if (head == NULL) {
    //         T::link(staged->tail, head, NULL);
    //     } else {
    //         T::link(staged->tail, head, head->next);
    //     }
    //     head = staged->head;
    //     if (tail == NULL) {
    //         tail = staged->tail;
    //     }
    //     size += staged->size;
    // }

    // // TODO: check if index bounds are correct.  Do they include the last item
    // // in the list?

    // /*Get the index of an item within the list.*/
    // size_t index(PyObject* item, long long start = 0, long long stop = -1) {
    //     T* curr = head;
    //     size_t i = 0;
    //     size_t norm_start = normalize_index(start);
    //     size_t norm_stop = normalize_index(stop);

    //     // skip to start index
    //     for (i; i < norm_start; i++) {
    //         if (curr == NULL) {
    //             throw std::out_of_range("list index out of range");
    //         }
    //         curr = curr->next;
    //     }

    //     int comp;

    //     // search until we hit stop index
    //     while (curr != NULL && i < norm_stop) {
    //         // C API equivalent of the == operator
    //         comp = PyObject_RichCompareBool(curr->value, item, Py_EQ)
    //         if (comp == -1) {  // comparison raised an exception
    //             return MAX_SIZE_T;
    //         } else if (comp == 1) {  // found a match
    //             return index;
    //         }
    
    //         // advance to next node
    //         curr = curr->next;
    //         i++;
    //     }

    //     // item not found
    //     PyObject* python_repr = PyObject_Repr(item);
    //     const char* c_repr = PyUnicode_AsUTF8(python_repr);
    //     Py_DECREF(python_repr);
    //     PyErr_Format(PyExc_ValueError, "%s is not in list", c_repr);
    //     return MAX_SIZE_T;
    // }

    // /*Count the number of occurrences of an item within the list.*/
    // size_t count(PyObject* item, long long start = 0, long long stop = -1) {
    //     T* curr = head;
    //     size_t i = 0;
    //     size_t observed = 0;
    //     size_t norm_start = normalize_index(start);
    //     size_t norm_stop = normalize_index(stop);

    //     // skip to start index
    //     for (i; i < norm_start; i++) {
    //         if (curr == NULL) {
    //             return observed;
    //         }
    //         curr = curr->next;
    //     }

    //     int comp;

    //     // search until we hit stop index
    //     while (curr != NULL && i < norm_stop) {
    //         // C API equivalent of the == operator
    //         comp = PyObject_RichCompareBool(curr->value, item, Py_EQ)
    //         if (comp == -1) {  // comparison raised an exception
    //             return MAX_SIZE_T;
    //         } else if (comp == 1) {  // found a match
    //             count++;
    //         }
    
    //         // advance to next node
    //         curr = curr->next;
    //         i++;
    //     }

    //     return observed;
    // }

    // /*Remove an item from the list.*/
    // int remove(PyObject* item) {
    //     T* curr = head;
    //     T* prev = NULL;  // shadows curr
    //     int comp;

    //     // remove first occurrence of item
    //     while (curr != NULL) {
    //         // C API equivalent of the == operator
    //         comp = PyObject_RichCompareBool(curr->value, item, Py_EQ)
    //         if (comp == -1) {  // comparison raised an exception
    //             return -1;
    //         } else if (comp == 1) {  // found a match
    //             unlink(prev, curr, curr->next);
    //             deallocate(curr);
    //             return 0;
    //         }

    //         // advance to next node
    //         prev = curr
    //         curr = curr->next;
    //     }

    //     // item not found
    //     PyObject* python_repr = PyObject_Repr(item);
    //     const char* c_repr = PyUnicode_AsUTF8(python_repr);
    //     Py_DECREF(python_repr);
    //     PyErr_Format(PyExc_ValueError, "%s is not in list", c_repr);
    //     return -1;
    // }

    // /*Check if the list contains a given item.*/
    // inline int contains(PyObject* item) {
    //     T* curr = head;
    //     int comp;

    //     // search until we hit stop index
    //     while (curr != NULL) {
    //         // C API equivalent of the == operator
    //         comp = PyObject_RichCompareBool(curr->value, item, Py_EQ)
    //         if (comp == -1) {  // comparison raised an exception
    //             return -1;
    //         } else if (comp == 1) {  // found a match
    //             return 1;
    //         }
    
    //         // advance to next node
    //         curr = curr->next;
    //     }

    //     return 0;
    // }

};


template <typename T>
class DictView : public ListView<T> {
public:
    ~DictView() {
        T* curr = this->head;
        T* next;
        while (curr != NULL) {
            next = curr->next;
            Py_DECREF(curr->value);
            Py_DECREF(curr->mapped);  // extra DECREF for mapped value
            free(curr);
            curr = next;
        }
    }

    T* allocate(PyObject* value, PyObject* mapped) {
        PyObject* python_repr;
        const char* c_repr;

        // print allocation message if DEBUG=TRUE
        if (DEBUG) {
            python_repr = PyObject_Repr(value);
            c_repr = PyUnicode_AsUTF8(python_repr);
            Py_DECREF(python_repr);
            printf("    -> malloc: %s\n", c_repr);
        }

        // delegate to node-specific allocator
        return T::allocate(this->freelist, value, mapped);
    }
};


#endif // NODE_H include guard
