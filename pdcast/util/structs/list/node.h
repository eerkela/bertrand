// include guard prevents multiple inclusion
#ifndef NODE_H
#define NODE_H


#include <cstddef>  // for size_t
#include <utility>  // for std::pair
#include <queue>  // for std::queue
#include <unordered_set>  // for std::unordered_set
#include <Python.h>  // for CPython API


/* TODO: set_slice() should be overridden by HashView and DictView to take an
additional argument for the ListTable.
*/


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


struct SingleNode {
    PyObject* value;
    SingleNode* next;

    /*static freelist constructor.*/
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

    /*static freelist destructor.*/
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

    /*copy constructor.*/
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


struct DoubleNode {
    PyObject* value;
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


struct HashNode {
    PyObject* value;
    Py_hash_t hash;
    HashNode* next;
    HashNode* prev;

    /*static freelist constructor.*/
    inline static HashNode* allocate(
        std::queue<HashNode*>& freelist,
        PyObject* value
    ) {        
        HashNode* node;

        // C API equivalent of the hash() function
        Py_hash_t hash = PyObject_Hash(value);
        if (hash == -1 && PyErr_Occurred()) {
            return NULL;
        }

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            node = (HashNode*)malloc(sizeof(HashNode));
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

    /*static freelist destructor.*/
    inline static void deallocate(
        std::queue<HashNode*>& freelist,
        HashNode* node
    ) {
        Py_DECREF(node->value);
        if (freelist.size() <= FREELIST_SIZE) {
            freelist.push(node);
        } else {
            free(node);
        }
    }

    /*copy constructor.*/
    inline static HashNode* copy(
        std::queue<HashNode*>& freelist,
        HashNode* node
    ) {
        // reuse the old node's hash value
        HashNode* new_node;

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            new_node = (HashNode*)malloc(sizeof(HashNode));
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

    /*Link the node to its neighbors to form a doubly-linked list.*/
    inline static void link(
        HashNode* prev,
        HashNode* curr,
        HashNode* next
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
        HashNode* prev,
        HashNode* curr,
        HashNode* next
    ) {
        if (prev != NULL) {
            prev->next = next;
        }
        if (next != NULL) {
            next->prev = prev;
        }
    }
};


struct DictNode {
    PyObject* value;
    PyObject* mapped;
    Py_hash_t hash;
    DictNode* next;
    DictNode* prev;

    /*static freelist constructor.*/
    inline static DictNode* allocate(
        std::queue<DictNode*>& freelist,
        PyObject* value,
        PyObject* mapped
    ) {
        DictNode* node;

        // C API equivalent of the hash() function
        Py_hash_t hash = PyObject_Hash(value);
        if (hash == -1 && PyErr_Occurred()) {
            return NULL;
        }

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            node = (DictNode*)malloc(sizeof(DictNode));
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
        node->mapped = mapped;
        node->next = NULL;
        node->prev = NULL;
        return node;
    }

    /*static freelist destructor.*/
    inline static void deallocate(
        std::queue<DictNode*>& freelist,
        DictNode* node
    ) {
        Py_DECREF(node->value);
        if (freelist.size() <= FREELIST_SIZE) {
            freelist.push(node);
        } else {
            free(node);
        }
    }

    /*copy constructor.*/
    inline static DictNode* copy(
        std::queue<DictNode*>& freelist,
        DictNode* node
    ) {
        // reuse the old node's hash and mapped value
        DictNode* new_node;

        // pop from free list if possible, else allocate a new node
        if (freelist.empty()) {
            new_node = (DictNode*)malloc(sizeof(DictNode));
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
        new_node->mapped = node->mapped;
        new_node->next = NULL;
        new_node->prev = NULL;
        return new_node;
    }

    /*Link the node to its neighbors to form a doubly-linked list.*/
    inline static void link(
        DictNode* prev,
        DictNode* curr,
        DictNode* next
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
        DictNode* prev,
        DictNode* curr,
        DictNode* next
    ) {
        if (prev != NULL) {
            prev->next = next;
        }
        if (next != NULL) {
            next->prev = prev;
        }
    }
};


/////////////////////
////    VIEWS    ////
/////////////////////


template <typename T>
class ListView {
private:
    std::queue<T*> freelist;

public:
    T* head;
    T* tail;
    size_t size;

    /*Construct a ListView with no nodes.*/
    ListView() {
        head = NULL;
        tail = NULL;
        size = 0;
        freelist = std::queue<T*>();
    }

    /*Destroy a ListView and free all the contained nodes.*/
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

    /*Deallocate a node from the list.*/
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

    /*Stage a linked list of nodes to be added to the list.*/
    ListView<T>* stage(PyObject* iterable, bool reverse = false) {
        // C API equivalent of iter(iterable)
        PyObject* iterator = PyObject_GetIter(iterable);
        if (iterator == NULL) {
            return NULL;
        }

        ListView<T>* staged = new ListView<T>();

        T* node;
        PyObject* item;
        size_t count = 0;

        while (true) {
            // C API equivalent of next(iterator)
            item = PyIter_Next(iterator);
            if (item == NULL) { // end of iterator or error
                if (PyErr_Occurred()) {
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
                T::link(NULL, node, staged->head);
                if (staged->tail == NULL) {
                    staged->tail = node;
                }
                staged->head = node;
            } else {
                T::link(staged->tail, node, NULL);
                if (staged->head == NULL) {
                    staged->head = node;
                }
                staged->tail = node;
            }

            // advance to next item
            count++;
            Py_DECREF(item);
        }

        // release reference on iterator
        Py_DECREF(iterator);

        // return the staged ListView
        staged->size = count;
        return staged;
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

    /*Get the direction to traverse a slice to minimize iterations and avoid
    backtracking.*/
    inline std::pair<size_t, size_t> get_slice_direction(
        size_t start,
        size_t stop,
        ssize_t step
    ) {
        size_t distance_from_head, distance_from_tail, index, end_index;

        if (step > 0) { // slice is ascending
            distance_from_head = start;
            distance_from_tail = size - stop;

            // traverse from nearest end
            if (distance_from_head <= distance_from_tail) {
                index = start;
                end_index = stop;
            } else {  // iterate over slice in reverse
                index = stop;
                end_index = start;
            }

        } else {  // slice is descending
            distance_from_head = stop;
            distance_from_tail = size - start;

            // traverse from nearest end
            if (distance_from_tail <= distance_from_head) {
                index = start;
                end_index = stop;
            } else {  // iterate over slice in reverse
                index = stop;
                end_index = start;
            }

        }

        return std::make_pair(index, end_index);
    }

    /*Get a node at a given index.*/
    T* node_at_index(size_t index) {
        T* curr;

        // iterate from nearest end
        if (index <= size / 2) {
            curr = head;
            for (size_t i = 0; i < index; i++) {
                curr = curr->next;
            }
        } else {
            curr = tail;
            for (size_t i = size - 1; i > index; i--) {
                curr = curr->prev;
            }
        }

        return curr;
    }

    /*Extract a slice from a linked list.*/
    ListView<T> get_slice(size_t start, size_t stop, ssize_t step) {
        ListView<T>* slice = new ListView<T>();
        std::pair<size_t, size_t> index;

        // determine direction of traversal to avoid backtracking
        index = get_slice_direction(start, stop, step);
        bool descending = (step < 0);
        size_t abs_step = (size_t)abs(step);

        // get first node in slice
        T* curr = node_at_index(index.first);
        T* copy;

        // copy all nodes in slice
        if (index.first <= index.second) {  // forward traversal
            while (curr != NULL && index.first <= index.second) {
                // TODO: catch bad_alloc
                copy = T::copy(slice->freelist, curr);
                if (descending) {
                    slice->link(NULL, copy, slice->head);
                } else {
                    slice->link(slice->tail, copy, NULL);
                }

                // jump according to step size
                index.first += abs_step;
                for (size_t i = 0; i < abs_step; i++) {
                    curr = curr->next;
                    if (curr == NULL) {
                        break;
                    }
                }
            }
        } else {  // backward traversal
            while (curr != NULL && index.first >= index.second) {
                // TODO: catch bad_alloc
                copy = T::copy(slice->freelist, curr);
                if (descending) {
                    slice->link(slice->tail, copy, NULL);
                } else {
                    slice->link(NULL, copy, slice->head);
                }

                // jump according to step size
                index.first -= abs_step;
                for (size_t i = 0; i < abs_step; i++) {
                    curr = curr->prev;
                    if (curr == NULL) {
                        break;
                    }
                }
            }
        }

        return slice;
    }

    /*Set a slice within a linked list.*/
    int set_slice(size_t start, size_t stop, ssize_t step, PyObject* iterator) {
        size_t abs_step = (size_t)abs(step);
        std::pair<size_t, size_t> index;

        // determine direction of traversal to avoid backtracking
        index = get_slice_direction(start, stop, step);

        // get first node in slice
        T* curr = node_at_index(index.first);

        // NOTE: we assume that the iterator is properly reversed if we are
        // traversing the slice opposite to `step`

        // assign to slice
        if (index.first <= index.second) {
            while (curr != NULL and index.first <= index.second) {
                // C API equivalent of next(iterator)
                PyObject* item = PyIter_Next(iterator);
                if (item == NULL) { // end of iterator or error
                    if (PyErr_Occurred()) {
                        Py_DECREF(item);
                        return -1;  // raise exception
                    }
                    break;
                }

                // assign to node (INCREF is handled by PyIter_Next())
                Py_DECREF(curr->value);
                curr->value = item;

                // jump according to step size
                index.first += abs_step;
                for (size_t i = 0; i < abs_step; i++) {
                    curr = curr->next;
                    if (curr == NULL) {
                        break;
                    }
                }
            }
        } else {
            while (curr != NULL and index.first >= index.second) {
                // C API equivalent of next(iterator)
                PyObject* item = PyIter_Next(iterator);
                if (item == NULL) { // end of iterator or error
                    if (PyErr_Occurred()) {
                        Py_DECREF(item);
                        return -1;  // raise exception
                    }
                    break;
                }

                // assign to node (INCREF is handled by PyIter_Next())
                Py_DECREF(curr->value);
                curr->value = item;

                // jump according to step size
                index.first -= abs_step;
                for (size_t i = 0; i < abs_step; i++) {
                    curr = curr->prev;
                    if (curr == NULL) {
                        break;
                    }
                }
            }
        }

        return 0;  // return 0 on success
    }

    /*Delete a slice within a linked list.*/
    void delete_slice(size_t start, size_t stop, ssize_t step) {
        std::pair<size_t, size_t> index;

        // determine direction of traversal to avoid backtracking
        index = get_slice_direction(start, stop, step);
        size_t abs_step = (size_t)abs(step);
        size_t small_step = abs_step - 1;  // we jump by 1 whenever we delete a node

        // get first node in slice
        T* curr = node_at_index(index.first);
        T* temp;

        // delete all nodes in slice
        if (index.first <= index.second) {  // forward traversal
            while (curr != NULL && index.first <= index.second) {
                temp = curr->next;
                unlink(curr->prev, curr, curr->next);
                deallocate(curr);
                curr = temp;

                // jump according to step size
                index.first += abs_step;
                for (size_t i = 0; i < small_step; i++) {
                    curr = curr->next;
                    if (curr == NULL) {
                        break;
                    }
                }
            }
        } else {  // backward traversal
            while (curr != NULL && index.first >= index.second) {
                temp = curr->prev;
                unlink(curr->prev, curr, curr->next);
                deallocate(curr);
                curr = temp;

                // jump according to step size
                index.first -= abs_step;
                for (size_t i = 0; i < small_step; i++) {
                    curr = curr->prev;
                    if (curr == NULL) {
                        break;
                    }
                }
            }
        }
    }

};


template <typename T>
class HashView : public ListView<T> {
public:

    // HashView<T>* stage(
    //     PyObject* iterable,
    //     bool reverse = false,
    //     std::unordered_set<T*>* override = NULL
    // ) {

    // }
};


template <typename T>
class DictView : public HashView<T> {
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
