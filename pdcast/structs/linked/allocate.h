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
#include "../util/string.h"  // repr()


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
////    LIST    ////
////////////////////


/* A custom allocator that uses a dynamic array to manage memory for each node. */
template <typename Node>
class ListAllocator {
private:
    static const size_t DEFAULT_CAPACITY = 8;  // minimum array size

    /* When we allocate new nodes, we fill the dynamic array from left to right.
    If a node is removed from the middle of the array, then we add it to the free
    list.  The next time a node is allocated, we check the free list and reuse a
    node if possible.  Otherwise, we initialize a new node at the end of the
    occupied section.  If this causes the array to exceed its capacity, then we
    allocate a new array with twice the length and copy all nodes in the same order
    as they appear in the list. */

    /* Allocate a raw array of uninitialized nodes with the specified size. */
    inline static Node* allocate_array(size_t capacity) {
        Node* result = static_cast<Node*>(malloc(capacity * sizeof(Node)));
        if (result == nullptr) {
            throw std::bad_alloc();
        }
        return result;
    }

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
        Node* new_array = allocate_array(new_capacity);
        if constexpr (DEBUG) {
            std::cout << "    -> allocate: " << new_capacity << " nodes" << std::endl;
        }

        // move nodes into new array
        transfer_nodes<false>(head, new_array);

        // replace old array
        free(array);  // bypasses destructors
        if constexpr (DEBUG) {
            std::cout << "    -> deallocate: " << capacity << " nodes" << std::endl;
        }
        array = new_array;
        capacity = new_capacity;
        free_list.first = nullptr;
        free_list.second = nullptr;

        // update head/tail pointers
        if (occupied != 0) {
            head = &(new_array[0]);
            tail = &(new_array[occupied - 1]);
        }
    }

    /* Initialize a node from the array to use in a list. */
    template <typename... Args>
    inline void init_node(Node* node, Args&&... args) {
        // variadic dispatch to node constructor
        new (node) Node(std::forward<Args>(args)...);

        // check python specialization if enabled
        if (specialization != nullptr && !node->typecheck(specialization)) {
            std::ostringstream msg;
            msg << util::repr(node->value()) << " is not of type ";
            msg << util::repr(specialization);
            node->~Node();  // in-place destructor
            throw util::type_error(msg.str());
        }

        if constexpr (DEBUG) {
            std::cout << "    -> create: " << util::repr(node->value()) << std::endl;
        }
    }

    /* Destroy all nodes contained in the list. */
    inline void destroy_list() noexcept {
        Node* curr = head;
        while (curr != nullptr) {
            Node* next = curr->next();
            if constexpr (DEBUG) {
                std::cout << "    -> recycle: " << util::repr(curr->value()) << std::endl;
            }
            curr->~Node();  // in-place destructor
            curr = next;
        }
    }

public:
    Node* head;  // head of the list
    Node* tail;  // tail of the list
    size_t capacity;  // number of nodes in the array
    size_t occupied;  // number of nodes currently in use
    bool frozen;  // indicates if the array is frozen at its current capacity
    Node* array;  // dynamic array of nodes
    std::pair<Node*, Node*> free_list;  // singly-linked list of open nodes
    PyObject* specialization;  // type specialization for PyObject* values

    /* Create an allocator with an optional fixed size. */
    ListAllocator(std::optional<size_t> capacity, PyObject* specialization) :
        head(nullptr),
        tail(nullptr),
        capacity(capacity.value_or(DEFAULT_CAPACITY)),
        occupied(0),
        frozen(capacity.has_value()),
        array(allocate_array(this->capacity)),
        free_list(std::make_pair(nullptr, nullptr)),
        specialization(specialization)
    {
        if constexpr (DEBUG) {
            std::cout << "    -> allocate: " << this->capacity << " nodes" << std::endl;
        }

        // increment reference count on specialization
        if (specialization != nullptr) {
            Py_INCREF(specialization);
        }
    }

    /* Copy constructor. */
    ListAllocator(const ListAllocator& other) :
        head(nullptr),
        tail(nullptr),
        capacity(other.capacity),
        occupied(other.occupied),
        frozen(other.frozen),
        array(allocate_array(capacity)),
        free_list(std::make_pair(nullptr, nullptr)),
        specialization(other.specialization)
    {
        if constexpr (DEBUG) {
            std::cout << "    -> allocate: " << capacity << " nodes" << std::endl;
        }

        // increment reference count on specialization
        Py_XINCREF(specialization);

        // copy over existing nodes in correct list order (head -> tail)
        if (occupied != 0) {
            transfer_nodes<true>(other.head, array);
            head = &array[0];
            tail = &array[occupied - 1];
        }
    }

    /* Move constructor. */
    ListAllocator(ListAllocator&& other) noexcept :
        head(other.head),
        tail(other.tail),
        capacity(other.capacity),
        occupied(other.occupied),
        frozen(other.frozen),
        array(other.array),
        free_list(std::move(other.free_list)),
        specialization(other.specialization)
    {
        other.head = nullptr;
        other.tail = nullptr;
        other.capacity = 0;
        other.occupied = 0;
        other.frozen = false;
        other.array = nullptr;
        other.free_list.first = nullptr;
        other.free_list.second = nullptr;
        other.specialization = nullptr;
    }

    /* Copy assignment operator. */
    ListAllocator& operator=(const ListAllocator& other) {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // deallocate current list
        Py_XDECREF(specialization);
        free_list.first = nullptr;
        free_list.second = nullptr;
        if (head != nullptr) {
            destroy_list();  // calls destructors
            head = nullptr;
            tail = nullptr;
        }
        if (array != nullptr) {
            free(array);
            if constexpr (DEBUG) {
                std::cout << "    -> deallocate: " << capacity << " nodes" << std::endl;
            }
        }

        // transfer metadata
        capacity = other.capacity;
        occupied = other.occupied;
        frozen = other.frozen;
        specialization = Py_XNewRef(other.specialization);

        // copy nodes
        array = allocate_array(capacity);
        if (occupied != 0) {
            transfer_nodes<true>(other.head, array);
            head = &array[0];
            tail = &array[occupied - 1];
        }

        return *this;
    }

    /* Move assignment operator. */
    ListAllocator& operator=(ListAllocator&& other) noexcept {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // clear current allocator
        Py_XDECREF(specialization);
        if (head != nullptr) {
            destroy_list();  // calls destructors
        }
        if (array != nullptr) {
            free(array);
            if constexpr (DEBUG) {
                std::cout << "    -> deallocate: " << capacity << " nodes" << std::endl;
            }
        }

        // transfer ownership
        head = other.head;
        tail = other.tail;
        capacity = other.capacity;
        occupied = other.occupied;
        frozen = other.frozen;
        array = other.array;
        free_list.first = other.free_list.first;
        free_list.second = other.free_list.second;
        specialization = other.specialization;

        // reset other allocator
        other.head = nullptr;
        other.tail = nullptr;
        other.capacity = 0;
        other.occupied = 0;
        other.frozen = false;
        other.array = nullptr;
        other.free_list.first = nullptr;
        other.free_list.second = nullptr;
        other.specialization = nullptr;
        return *this;
    }

    /* Destroy an allocator and release its resources. */
    ~ListAllocator() noexcept {
        Py_XDECREF(specialization);
        if (head != nullptr) {
            destroy_list();  // calls destructors
        }
        if (array != nullptr) {
            free(array);
            if constexpr (DEBUG) {
                std::cout << "    -> deallocate: " << capacity << " nodes" << std::endl;
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
                init_node(node, std::forward<Args>(args)...);
            } catch (...) {
                node->next(temp);  // restore free list
                throw;  // propagate
            }
            free_list.first = temp;
            if (temp == nullptr) {
                free_list.second = nullptr;
            }
            ++occupied;
            return node;
        }

        // check if we need to grow the array
        if (occupied == capacity) {
            if (frozen) {
                std::ostringstream msg;
                msg << "array cannot grow beyond size " << capacity;
                throw std::runtime_error(msg.str());
            }
            resize(capacity * 2);
        }

        // append to end of allocated section
        Node* node = &array[occupied++];
        init_node(node, std::forward<Args>(args)...);
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
        if (!frozen && capacity != DEFAULT_CAPACITY && occupied == capacity / 4) {
            resize(capacity / 2);
        } else {
            if (free_list.first == nullptr) {
                free_list.first = node;
                free_list.second = node;
            } else {
                free_list.second->next(node);
                free_list.second = node;
            }
        }
        --occupied;
    }

    /* Remove all elements from a list. */
    void clear() noexcept {
        // destroy all nodes
        destroy_list();

        // reset list parameters
        head = nullptr;
        tail = nullptr;
        occupied = 0;
        free_list.first = nullptr;
        free_list.second = nullptr;
        if (!frozen) {
            capacity = DEFAULT_CAPACITY;
            free(array);
            array = allocate_array(capacity);
        }
    }

    /* Resize the array to house a specific number of nodes. */
    void reserve(size_t new_capacity) {
        // ensure new capacity is large enough to store all nodes
        if (new_capacity < occupied) {
            throw std::invalid_argument(
                "new capacity must not be smaller than current size"
            );
        }

        // handle frozen arrays
        if (frozen) {
            if (new_capacity <= capacity) {
                return;  // do nothing
            }
            std::ostringstream msg;
            msg << "array cannot grow beyond size " << capacity;
            throw std::runtime_error(msg.str());
        }

        // ensure array does not shrink below default capacity
        if (new_capacity <= DEFAULT_CAPACITY) {
            if (capacity != DEFAULT_CAPACITY) {
                resize(DEFAULT_CAPACITY);
            }
            return;
        }

        // resize to the next power of two
        size_t rounded = util::next_power_of_two(new_capacity);
        if (rounded != capacity) {
            resize(rounded);
        }
    }

    /* Consolidate the nodes within the array, arranging them in the same order as
    they appear within the list. */
    inline void consolidate() {
        resize(capacity);  // in-place resize
    }

    /* Check whether the referenced node is being managed by this allocator. */
    inline bool owns(Node* node) const noexcept {
        return node >= array && node < array + capacity;  // pointer arithmetic
    }

    /* Enforce strict type checking for python values within a list. */
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


//////////////////////////////
////    SET/DICTIONARY    ////
//////////////////////////////


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_ALLOCATE_H
