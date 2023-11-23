// include guard: BERTRAND_STRUCTS_LINKED_CORE_ALLOCATE_H
#ifndef BERTRAND_STRUCTS_LINKED_CORE_ALLOCATE_H
#define BERTRAND_STRUCTS_LINKED_CORE_ALLOCATE_H

#include <cstddef>  // size_t
#include <cstdlib>  // malloc(), calloc(), free()
#include <functional>  // std::hash
#include <iostream>  // std::cout, std::endl
#include <optional>  // std::optional
#include <sstream>  // std::ostringstream
#include <stdexcept>  // std::invalid_argument
#include <Python.h>  // CPython API
#include "../../util/except.h"  // catch_python(), TypeError(), KeyError()
#include "../../util/math.h"  // next_power_of_two()
#include "../../util/python.h"  // std::hash<PyObject*>, eq(), len()
#include "../../util/repr.h"  // repr()
#include "../../util/name.h"  // PyName
#include "node.h"  // NodeTraits


namespace bertrand {
namespace structs {
namespace linked {


/////////////////////////
////    CONSTANTS    ////
/////////////////////////


/* DEBUG=TRUE adds print statements for every memory allocation in order to help catch
leaks. */
const bool DEBUG = true;


////////////////////
////    BASE    ////
////////////////////


/* Empty tag class marking a node allocator for a linked data structure.

NOTE: this class is inherited by all allocators, and can be used for easy SFINAE checks
via std::is_base_of, without requiring any foreknowledge of template parameters. */
class AllocatorTag {};


/* Base class that implements shared functionality for all allocators and provides the
minimum necessary attributes for compatibility with higher-level views. */
template <typename Derived, typename NodeType>
class BaseAllocator : public AllocatorTag {
public:
    using Node = NodeType;
    class MemGuard;
    class PyMemGuard;

protected:
    // bitfield for storing boolean flags related to allocator state, as follows:
    // 0b001: if set, allocator is temporarily frozen for memory stability
    // 0b010: if set, allocator does not support dynamic resizing
    // 0b100: if set, nodes cannot be re-specialization after list construction
    enum {
        FROZEN                  = 0b001,
        DYNAMIC                 = 0b010,
        STRICTLY_TYPED          = 0b100  // (not used)
    } CONFIG;
    unsigned char config;
    union { mutable Node _temp; };  // uninitialized temporary node for internal use

    /* Allocate a contiguous block of uninitialized items with the specified size. */
    inline static Node* malloc_nodes(size_t capacity) {
        Node* result = static_cast<Node*>(std::malloc(capacity * sizeof(Node)));
        if (result == nullptr) {
            throw std::bad_alloc();
        }
        return result;
    }

    /* Initialize an uninitialized node for use in the list. */
    template <typename... Args>
    inline void init_node(Node* node, Args&&... args) {
        using util::repr;

        // variadic dispatch to node constructor
        new (node) Node(std::forward<Args>(args)...);

        // check python specialization if enabled
        if (specialization != nullptr && !node->typecheck(specialization)) {
            std::ostringstream msg;
            msg << repr(node->value()) << " is not of type ";
            msg << repr(specialization);
            node->~Node();  // in-place destructor
            throw util::TypeError(msg.str());
        }
        if constexpr (DEBUG) {
            std::cout << "    -> create: " << repr(node->value()) << std::endl;
        }
    }

    /* Destroy all nodes contained in the list. */
    inline void destroy_list() noexcept {
        using util::repr;

        Node* curr = head;
        while (curr != nullptr) {
            Node* next = curr->next();
            if constexpr (DEBUG) {
                std::cout << "    -> recycle: " << repr(curr->value()) << std::endl;
            }
            curr->~Node();  // in-place destructor
            curr = next;
        }
    }

    /* Shrink a dynamic allocator if it is under the minimum load factor.  This is
    called automatically by recycle() as well as when a MemGuard falls out of scope,
    guaranteeing the load factor is never less than 25% of the list's capacity. */
    inline bool shrink() {
        if (!this->frozen() &&
            this->dynamic() &&
            this->capacity != Derived::DEFAULT_CAPACITY &&
            this->occupied <= this->capacity / 4
        ) {
            Derived* self = static_cast<Derived*>(this);
            size_t size = util::next_power_of_two(this->occupied * 2);
            size = size < Derived::DEFAULT_CAPACITY ? Derived::DEFAULT_CAPACITY : size;
            self->resize(size);
            return true;
        }
        return false;
    }

    /* Throw an error indicating that the allocator is frozen at its current size. */
    inline auto cannot_grow() const {
        std::ostringstream msg;
        msg << "allocator is frozen at size " << capacity;
        return util::MemoryError(msg.str());
    }

    /* Create an allocator with an optional fixed size. */
    BaseAllocator(
        size_t capacity,
        bool dynamic,
        PyObject* specialization
    ) : config(DYNAMIC * dynamic),
        head(nullptr),
        tail(nullptr),
        capacity(capacity),
        occupied(0),
        specialization(specialization)
    {
        if constexpr (DEBUG) {
            std::cout << "    -> allocate: " << this->capacity << " nodes" << std::endl;
        }
        if (specialization != nullptr) {
            Py_INCREF(specialization);
        }
    }

public:
    Node* head;  // head of the list
    Node* tail;  // tail of the list
    size_t capacity;  // number of nodes in the array
    size_t occupied;  // number of nodes currently in use - equivalent to list.size()
    PyObject* specialization;  // type specialization for PyObject* values

    /* Copy constructor. */
    BaseAllocator(const BaseAllocator& other) :
        config(other.config),
        head(nullptr),
        tail(nullptr),
        capacity(other.capacity),
        occupied(other.occupied),
        specialization(other.specialization)
    {
        if constexpr (DEBUG) {
            std::cout << "    -> allocate: " << capacity << " nodes" << std::endl;
        }
        Py_XINCREF(specialization);
    }

    /* Move constructor. */
    BaseAllocator(BaseAllocator&& other) noexcept :
        config(other.config),
        head(other.head),
        tail(other.tail),
        capacity(other.capacity),
        occupied(other.occupied),
        specialization(other.specialization)
    {
        other.config = 0;
        other.head = nullptr;
        other.tail = nullptr;
        other.capacity = 0;
        other.occupied = 0;
        other.specialization = nullptr;
    }

    /* Copy assignment operator. */
    BaseAllocator& operator=(const BaseAllocator& other) {
        if (this == &other) return *this;  // check for self-assignment
        if (frozen()) {
            throw util::MemoryError(
                "array cannot be reallocated while a MemGuard is active"
            );
        }

        // destroy current
        Py_XDECREF(specialization);
        if (head != nullptr) {
            destroy_list();  // calls destructors
            head = nullptr;
            tail = nullptr;
        }
        if constexpr (DEBUG) {
            std::cout << "    -> deallocate: " << capacity << " nodes" << std::endl;
        }

        // copy from other
        config = other.config;
        capacity = other.capacity;
        occupied = other.occupied;
        specialization = Py_XNewRef(other.specialization);

        return *this;
    }

    /* Move assignment operator. */
    BaseAllocator& operator=(BaseAllocator&& other) noexcept {
        if (this == &other) return *this;  // check for self-assignment
        if (frozen()) {
            throw util::MemoryError(
                "array cannot be reallocated while a MemGuard is active"
            );
        }

        // destroy current
        Py_XDECREF(specialization);
        if (head != nullptr) {
            destroy_list();  // calls destructors
        }
        if constexpr (DEBUG) {
            std::cout << "    -> deallocate: " << capacity << " nodes" << std::endl;
        }

        // transfer ownership
        config = other.config;
        head = other.head;
        tail = other.tail;
        capacity = other.capacity;
        occupied = other.occupied;
        specialization = other.specialization;

        // reset other
        other.config = 0;
        other.head = nullptr;
        other.tail = nullptr;
        other.capacity = 0;
        other.occupied = 0;
        other.specialization = nullptr;
        return *this;
    }

    /* Destroy an allocator and release its resources. */
    ~BaseAllocator() noexcept {
        Py_XDECREF(specialization);
    }

    ////////////////////////
    ////    ABSTRACT    ////
    ////////////////////////

    /* Construct a new node for the list. */
    template <typename... Args>
    Node* create(Args&&...);

    /* Release a node from the list. */
    void recycle(Node* node) {
        // manually call destructor
        if constexpr (DEBUG) {
            std::cout << "    -> recycle: " << util::repr(node->value());
            std::cout << std::endl;
        }
        node->~Node();
        --occupied;
    }

    /* Remove all elements from the list. */
    void clear() noexcept {
        // destroy all nodes
        destroy_list();

        // reset list parameters
        head = nullptr;
        tail = nullptr;
        occupied = 0;
    }

    /////////////////////////
    ////    INHERITED    ////
    /////////////////////////

    /* Resize the allocator to store a specific number of nodes. */
    MemGuard reserve(size_t new_size) {
        // ensure new capacity is large enough to store all existing nodes
        if (new_size < this->occupied) {
            throw std::invalid_argument(
                "new capacity cannot be smaller than current size"
            );
        }

        // if frozen, check new size against current capacity
        if (this->frozen() || !this->dynamic()) {
            if (new_size > this->capacity) {
                throw cannot_grow();  // throw error
            } else {
                return MemGuard();  // return empty guard
            }
        }

        // otherwise, grow the array if necessary
        Derived* self = static_cast<Derived*>(this);
        size_t new_capacity = util::next_power_of_two(new_size);
        if (new_capacity > Derived::DEFAULT_CAPACITY && new_capacity > this->capacity) {
            self->resize(new_capacity);
        }

        // freeze allocator until guard falls out of scope
        return MemGuard(self);
    }

    /* Attempt to resize the allocator based on an optional size. */
    inline MemGuard try_reserve(std::optional<size_t> new_size) {
        if (!new_size.has_value()) {
            return MemGuard();
        }
        return reserve(new_size.value());
    }

    /* Attempt to reserve memory to hold all the elements of a given container if it
    implements a `size()` method or is a Python object with a corresponding `__len__()`
    attribute.  Otherwise, produce an empty MemGuard. */
    template <typename Container>
    inline MemGuard try_reserve(Container& container) {
        std::optional<size_t> length = util::len(container);
        if (!length.has_value()) {
            return MemGuard();
        }
        return reserve(this->occupied + length.value());
    }

    /* Rearrange the nodes in memory to reduce fragmentation. */
    void defragment() {
        // ensure list is not frozen for memory stability
        if (frozen()) {
            std::ostringstream msg;
            msg << "array cannot be reallocated while a MemGuard is active";
            throw util::MemoryError(msg.str());
        }

        // invoke derived method
        static_cast<Derived*>(this)->resize(capacity);  // in-place reallocate
    }

    /* Enforce strict type checking for python values within the list. */
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
                throw util::catch_python<util::TypeError>();
            } else if (comp == 1) {
                return;
            }
        }

        // check the contents of the list
        Node* curr = head;
        while (curr != nullptr) {
            if (!curr->typecheck(spec)) {
                std::ostringstream msg;
                msg << util::repr(curr->value()) << " is not of type ";
                msg << util::repr(spec);
                throw util::TypeError(msg.str());
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

    /* Get a temporary node for internal use. */
    inline Node* temp() const noexcept {
        return &_temp;
    }

    /* Check whether the allocator supports dynamic resizing. */
    inline bool dynamic() const noexcept {
        return static_cast<bool>(config & DYNAMIC);
    }

    /* Check whether the allocator is temporarily frozen for memory stability. */
    inline bool frozen() const noexcept {
        return static_cast<bool>(config & FROZEN);
    }

    /* Get the total amount of dynamic memory allocated by this allocator. */
    inline size_t nbytes() const noexcept {
        return (1 + this->capacity) * sizeof(Node);  // account for temporary node
    }

    //////////////////////////////
    ////    NESTED CLASSES    ////
    //////////////////////////////

    /* An RAII-style memory guard that temporarily prevents an allocator from being resized
    or defragmented within a certain context. */
    class MemGuard {
        friend BaseAllocator;
        friend Derived;
        friend PyMemGuard;
        Derived* allocator;

        /* Create an active MemGuard for an allocator, freezing it at its current
        capacity. */
        MemGuard(Derived* allocator) noexcept : allocator(allocator) {
            allocator->config |= FROZEN;  // set frozen bitflag
            if constexpr (DEBUG) {
                std::cout << "FREEZE: " << allocator->capacity << " NODES";
                std::cout << std::endl;
            }
        }

        /* Create an inactive MemGuard for an allocator. */
        MemGuard() noexcept : allocator(nullptr) {}

        /* Destroy the outermost MemGuard. */
        inline void destroy() noexcept {
            allocator->config &= ~FROZEN;  // clear frozen bitflag
            if constexpr (DEBUG) {
                std::cout << "UNFREEZE: " << allocator->capacity << " NODES";
                std::cout << std::endl;
            }
            allocator->shrink();  // every allocator implements this
        }

    public:

        /* Copy constructor/assignment operator deleted for safety. */
        MemGuard(const MemGuard& other) = delete;
        MemGuard& operator=(const MemGuard& other) = delete;

        /* Move constructor. */
        MemGuard(MemGuard&& other) noexcept : allocator(other.allocator) {
            other.allocator = nullptr;
        }

        /* Move assignment operator. */
        MemGuard& operator=(MemGuard&& other) noexcept {
            // check for self-assignment
            if (this == &other) {
                return *this;
            }

            // destroy current
            if (active()) destroy();

            // transfer ownership
            allocator = other.allocator;
            other.allocator = nullptr;
            return *this;
        }

        /* Unfreeze and potentially shrink the allocator when the outermost MemGuard falls
        out of scope. */
        ~MemGuard() noexcept { if (active()) destroy(); }

        /* Check whether the guard is active. */
        inline bool active() const noexcept { return allocator != nullptr; }

    };

    /* A Python wrapper around a MemGuard that allows it to be used as a context manager. */
    class PyMemGuard {
        PyObject_HEAD
        Derived* allocator;
        size_t capacity;
        bool has_guard;
        union { MemGuard guard; };

    public:

        /* Disable instantiation from C++. */
        PyMemGuard() = delete;
        PyMemGuard(const PyMemGuard&) = delete;
        PyMemGuard(PyMemGuard&&) = delete;
        PyMemGuard& operator=(const PyMemGuard&) = delete;
        PyMemGuard& operator=(PyMemGuard&&) = delete;

        /* Construct a Python MemGuard for a C++ allocator. */
        inline static PyObject* construct(Derived* allocator, size_t capacity) {
            // allocate
            PyMemGuard* self = PyObject_New(PyMemGuard, &Type);
            if (self == nullptr) {
                PyErr_SetString(
                    PyExc_RuntimeError,
                    "failed to allocate PyMemGuard"
                );
                return nullptr;
            }

            // initialize
            self->allocator = allocator;
            self->capacity = capacity;
            self->has_guard = false;
            return reinterpret_cast<PyObject*>(self);
        }

        /* Enter the context manager's block, freezing the allocator. */
        static PyObject* __enter__(PyMemGuard* self, PyObject* /* ignored */) {
            // check if the allocator is already frozen
            if (self->has_guard) {
                PyErr_SetString(
                    PyExc_RuntimeError,
                    "allocator is already frozen"
                );
                return nullptr;
            }

            // freeze the allocator
            try {
                new (&self->guard) MemGuard(self->allocator->reserve(self->capacity));

            // translate C++ exceptions into Python errors
            } catch (...) {
                util::throw_python();
                return nullptr;
            }

            // return new reference
            self->has_guard = true;
            return Py_NewRef(self);
        }

        /* Exit the context manager's block, unfreezing the allocator. */
        inline static PyObject* __exit__(PyMemGuard* self, PyObject* /* ignored */) {
            if (self->has_guard) {
                self->guard.~MemGuard();
                self->has_guard = false;
            }
            Py_RETURN_NONE;
        }

        /* Check if the allocator is currently frozen. */
        inline static PyObject* active(PyMemGuard* self, void* /* ignored */) {
            return PyBool_FromLong(self->has_guard);
        }

    private:

        /* Unfreeze the allocator when the context manager is garbage collected, if it
        hasn't been unfrozen already. */
        inline static void __dealloc__(PyMemGuard* self) {
            if (self->has_guard) {
                self->guard.~MemGuard();
                self->has_guard = false;
            }
            Type.tp_free(self);
        }

        /* Docstrings for public attributes of PyMemGuard. */
        struct docs {

            static constexpr std::string_view PyMemGuard {R"doc(
A Python-compatible wrapper around a C++ MemGuard that allows it to be used as
a context manager.

Notes
-----
This class is only meant to be instantiated via the ``reserve()`` method of a
linked data structure.  It is directly equivalent to constructing a C++
RAII-style MemGuard within the guarded context.  The C++ guard is automatically
destroyed upon exiting the context.
)doc"
            };

            static constexpr std::string_view __enter__ {R"doc(
Enter the context manager's block, freezing the allocator.

Returns
-------
PyMemGuard
    The context manager itself, which may be aliased using the `as` keyword.
)doc"
            };

            static constexpr std::string_view __exit__ {R"doc(
Exit the context manager's block, unfreezing the allocator.
)doc"
            };

            static constexpr std::string_view active {R"doc(
Check if the allocator is currently frozen.

Returns
-------
bool
    True if the allocator is currently frozen, False otherwise.
)doc"
            };

        };

        /* Vtable containing Python @properties for the context manager. */
        inline static PyGetSetDef properties[] = {
            {"active", (getter) active, NULL, docs::active.data()},
            {NULL}  // sentinel
        };

        /* Vtable containing Python methods for the context manager. */
        inline static PyMethodDef methods[] = {
            {"__enter__", (PyCFunction) __enter__, METH_NOARGS, docs::__enter__.data()},
            {"__exit__", (PyCFunction) __exit__, METH_VARARGS, docs::__exit__.data()},
            {NULL}  // sentinel
        };

        /* Initialize a PyTypeObject to represent the guard from Python. */
        static PyTypeObject init_type() {
            PyTypeObject slots = {
                .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
                .tp_name = util::PyName<MemGuard>.data(),
                .tp_basicsize = sizeof(PyMemGuard),
                .tp_dealloc = (destructor) __dealloc__,
                .tp_flags = (
                    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_IMMUTABLETYPE |
                    Py_TPFLAGS_DISALLOW_INSTANTIATION
                ),
                .tp_doc = docs::PyMemGuard.data(),
                .tp_methods = methods,
                .tp_getset = properties,
            };

            // register Python type
            if (PyType_Ready(&slots) < 0) {
                throw std::runtime_error("could not initialize PyMemGuard type");
            }
            return slots;
        }

    public:

        /* Final Python type. */
        inline static PyTypeObject Type = init_type();

    };

};


////////////////////
////    LIST    ////
////////////////////


/* A custom allocator that uses a dynamic array to manage memory for each node. */
template <typename NodeType>
class ListAllocator : public BaseAllocator<ListAllocator<NodeType>, NodeType> {
    using Base = BaseAllocator<ListAllocator<NodeType>, NodeType>;
    friend Base;
    friend typename Base::MemGuard;

public:
    using Node = typename Base::Node;
    static constexpr size_t DEFAULT_CAPACITY = 8;  // minimum array size

private:
    Node* array;  // dynamic array of nodes
    std::pair<Node*, Node*> free_list;  // singly-linked list of open nodes

    /* When we allocate new nodes, we fill the dynamic array from left to right.
    If a node is removed from the middle of the array, then we add it to the free
    list.  The next time a node is allocated, we check the free list and reuse a
    node if possible.  Otherwise, we initialize a new node at the end of the
    occupied section.  If this causes the array to exceed its capacity, then we
    allocate a new array with twice the length and copy all nodes in the same order
    as they appear in the list. */

    /* Adjust the starting capacity of a dynamic list to a power of two. */
    inline static size_t init_capacity(std::optional<size_t> capacity, bool dynamic) {
        if (!capacity.has_value()) {
            return DEFAULT_CAPACITY;
        }

        // if list is dynamic, round up to next power of two
        size_t result = capacity.value();
        if (dynamic) {
            return result < DEFAULT_CAPACITY ?
                DEFAULT_CAPACITY :
                util::next_power_of_two(result);
        }

        // otherwise, return as-is
        return result;
    }

    /* Copy/move the nodes from this allocator into the given array. */
    template <bool move>
    std::pair<Node*, Node*> transfer(Node* other) const {
        Node* new_head = nullptr;
        Node* new_tail = nullptr;

        // copy over existing nodes
        Node* curr = this->head;
        size_t idx = 0;
        while (curr != nullptr) {
            // remember next node in original list
            Node* next = curr->next();

            // initialize new node in array
            Node* other_curr = &other[idx++];
            if constexpr (move) {
                new (other_curr) Node(std::move(*curr));
            } else {
                new (other_curr) Node(*curr);
            }

            // link to previous node in array
            if (curr == this->head) {
                new_head = other_curr;
            } else {
                Node::join(new_tail, other_curr);
            }
            new_tail = other_curr;
            curr = next;
        }

        // return head/tail pointers for new list
        return std::make_pair(new_head, new_tail);
    }

    /* Allocate a new array of a given size and transfer the contents of the list. */
    void resize(size_t new_capacity) {
        // allocate new array
        Node* new_array = Base::malloc_nodes(new_capacity);
        if constexpr (DEBUG) {
            std::cout << "    -> allocate: " << new_capacity << " nodes" << std::endl;
        }

        // move nodes into new array
        std::pair<Node*, Node*> bounds(transfer<true>(new_array));
        this->head = bounds.first;
        this->tail = bounds.second;

        // replace old array
        free(array);  // bypasses destructors
        if constexpr (DEBUG) {
            std::cout << "    -> deallocate: " << this->capacity << " nodes" << std::endl;
        }
        array = new_array;
        free_list.first = nullptr;
        free_list.second = nullptr;
        this->capacity = new_capacity;
    }

public:

    /* Create an allocator with an optional fixed size. */
    ListAllocator(
        std::optional<size_t> capacity,
        bool dynamic,
        PyObject* specialization
    ) : Base(init_capacity(capacity, dynamic), dynamic, specialization),
        array(Base::malloc_nodes(this->capacity)),
        free_list(std::make_pair(nullptr, nullptr))
    {}

    /* Copy constructor. */
    ListAllocator(const ListAllocator& other) :
        Base(other),
        array(Base::malloc_nodes(this->capacity)),
        free_list(std::make_pair(nullptr, nullptr))
    {
        if (this->occupied) {
            std::pair<Node*, Node*> bounds(other.template transfer<false>(array));
            this->head = bounds.first;
            this->tail = bounds.second;
        }
    }

    /* Move constructor. */
    ListAllocator(ListAllocator&& other) noexcept :
        Base(std::move(other)),
        array(other.array),
        free_list(std::move(other.free_list))
    {
        other.array = nullptr;
        other.free_list.first = nullptr;
        other.free_list.second = nullptr;
    }

    /* Copy assignment operator. */
    ListAllocator& operator=(const ListAllocator& other) {
        if (this == &other) return *this;  // check for self-assignment

        // invoke parent operator
        Base::operator=(other);

        // destroy array
        free_list.first = nullptr;
        free_list.second = nullptr;
        if (array != nullptr) {
            free(array);
        }

        // copy array
        array = Base::malloc_nodes(this->capacity);
        if (this->occupied != 0) {
            std::pair<Node*, Node*> bounds(other.template transfer<false>(array));
            this->head = bounds.first;
            this->tail = bounds.second;
        } else {
            this->head = nullptr;
            this->tail = nullptr;
        }
        return *this;
    }

    /* Move assignment operator. */
    ListAllocator& operator=(ListAllocator&& other) noexcept {
        if (this == &other) return *this;  // check for self-assignment

        // invoke parent
        Base::operator=(std::move(other));

        // destroy array
        if (array != nullptr) {
            free(array);
        }

        // transfer ownership
        array = other.array;
        free_list.first = other.free_list.first;
        free_list.second = other.free_list.second;

        // reset other
        other.array = nullptr;
        other.free_list.first = nullptr;
        other.free_list.second = nullptr;
        return *this;
    }

    /* Destroy an allocator and release its resources. */
    ~ListAllocator() noexcept {
        if (this->occupied) {
            Base::destroy_list();  // calls destructors
        }
        if (array != nullptr) {
            free(array);
            if constexpr (DEBUG) {
                std::cout << "    -> deallocate: " << this->capacity << " nodes";
                std::cout << std::endl;
            }
        }
    }

    /* Construct a new node for the list. */
    template <typename... Args>
    Node* create(Args&&... args) {
        // check free list
        if (free_list.first != nullptr) {
            Node* node = free_list.first;
            Node* temp = node->next();
            try {
                Base::init_node(node, std::forward<Args>(args)...);
            } catch (...) {
                node->next(temp);  // restore free list
                throw;  // propagate
            }
            free_list.first = temp;
            if (free_list.first == nullptr) free_list.second = nullptr;
            ++this->occupied;
            return node;
        }

        // check if we need to grow the array
        if (this->occupied == this->capacity) {
            if (this->frozen() || !this->dynamic()) {
                throw Base::cannot_grow();
            }
            resize(this->capacity * 2);
        }

        // append to end of allocated section
        Node* node = &array[this->occupied];
        Base::init_node(node, std::forward<Args>(args)...);
        ++this->occupied;
        return node;
    }

    /* Release a node from the list. */
    void recycle(Node* node) {
        // invoke parent method
        Base::recycle(node);

        // shrink array if necessary, else add to free list
        if (!this->shrink()) {
            if (free_list.first == nullptr) {
                free_list.first = node;
                free_list.second = node;
            } else {
                free_list.second->next(node);
                free_list.second = node;
            }
        }
    }

    /* Remove all elements from the list. */
    void clear() noexcept {
        // invoke parent method
        Base::clear();

        // reset free list and shrink to default capacity
        free_list.first = nullptr;
        free_list.second = nullptr;
        if (!this->frozen() && this->dynamic()) {
            this->capacity = DEFAULT_CAPACITY;
            free(array);
            array = Base::malloc_nodes(this->capacity);
        }
    }

    /* Check whether the referenced node is being managed by this allocator. */
    inline bool owns(Node* node) const noexcept {
        return node >= array && node < array + this->capacity;  // pointer arithmetic
    }

};


//////////////////////////////
////    SET/DICTIONARY    ////
//////////////////////////////


/* Enumeration of all the collision resolution strategies that can be used in
HashAllocators.  */
enum class Collision {
    DOUBLE_HASH,  // separate, power of 2 open address table w/ tombstones
    HOPSCOTCH  // direct hopscotch table w/ neighborhood size of 31
};


/* Generic class for a HashAllocator implementing one of the above collision
policies. */
template <typename NodeType, Collision Policy>
class HashAllocator;


/* An extension of a ListAllocator that maintains a separate hash map of occupied
nodes.  Uses double hashing with a strict power of two table size to resolve
collisions.  Removed nodes are replaced by tombstones, leading to periodic rehashes. */
template <typename NodeType>
class HashAllocator<NodeType, Collision::DOUBLE_HASH> :
    public BaseAllocator<HashAllocator<NodeType, Collision::DOUBLE_HASH>, NodeType>
{
    using Base = BaseAllocator<HashAllocator<NodeType, Collision::DOUBLE_HASH>, NodeType>;
    friend Base;
    friend typename Base::MemGuard;

public:
    using Node = typename Base::Node;
    static constexpr size_t DEFAULT_CAPACITY = 8;  // minimum array size

private:
    using Value = typename Node::Value;
    static constexpr size_t PRIMES[] = {  // prime numbers to use for double hashing
        // HASH PRIME       // TABLE SIZE
        0,                  // 1 (2**0)         (not used)
        1,                  // 2 (2**1)         (not used)
        3,                  // 4 (2**2)         (not used)
        7,                  // 8 (2**3)         (not used)
        13,                 // 16 (2**4)
        23,                 // 32 (2**5)
        53,                 // 64 (2**6)
        97,                 // 128 (2**7)
        193,                // 256 (2**8)
        389,                // 512 (2**9)
        769,                // 1024 (2**10)
        1543,               // 2048 (2**11)
        3079,               // 4096 (2**12)
        6151,               // 8192 (2**13)
        12289,              // 16384 (2**14)
        24593,              // 32768 (2**15)
        49157,              // 65536 (2**16)
        98317,              // 131072 (2**17)
        196613,             // 262144 (2**18)
        393241,             // 524288 (2**19)
        786433,             // 1048576 (2**20)
        1572869,            // 2097152 (2**21)
        3145739,            // 4194304 (2**22)
        6291469,            // 8388608 (2**23)
        12582917,           // 16777216 (2**24)
        25165843,           // 33554432 (2**25)
        50331653,           // 67108864 (2**26)
        100663319,          // 134217728 (2**27)
        201326611,          // 268435456 (2**28)
        402653189,          // 536870912 (2**29)
        805306457,          // 1073741824 (2**30)
        1610612741,         // 2147483648 (2**31)
        3221225473,         // 4294967296 (2**32)
        // NOTE: HASH PRIME is the first prime number larger than 0.75 * TABLE_SIZE
    };

    Node* array;  // dynamic array of nodes
    std::pair<Node*, Node*> free_list;  // singly-linked list of open nodes
    size_t table_size;  // size of hash table
    size_t tombstones;  // number of nodes marked for deletion
    unsigned char exponent;  // log2(table_size) - log2(DEFAULT_CAPACITY)
    size_t prime;  // prime number used for double hashing
    Node** table;  // hash table of node addresses

    /* Adjust the starting capacity of a dynamic set to a power of two. */
    inline static size_t init_capacity(std::optional<size_t> capacity, bool dynamic) {
        if (!capacity.has_value()) {
            return DEFAULT_CAPACITY;
        }

        // if list is dynamic, round up to next power of two
        size_t result = capacity.value();
        if (dynamic) {
            return result < DEFAULT_CAPACITY ?
                DEFAULT_CAPACITY :
                util::next_power_of_two(result);
        }

        // otherwise, return as-is
        return result;
    }

    /* Allocate a zero-initialized table of node addresses with the specified size. */
    inline static Node** calloc_table(size_t size) {
        Node** result = static_cast<Node**>(
            std::calloc(size, sizeof(Node*))
        );
        if (result == nullptr) {
            throw std::bad_alloc();
        }
        return result;
    }

    /* Copy/move the nodes from this allocator into the given array + hash table. */
    template <bool move>
    std::pair<Node*, Node*> transfer(Node* other, Node** table) const {
        Node* new_head = nullptr;
        Node* new_tail = nullptr;

        // copy over existing nodes
        Node* curr = this->head;
        size_t idx = 0;
        while (curr != nullptr) {
            // remember next node in original list
            Node* next = curr->next();

            // initialize new node in array
            Node* other_curr = &other[idx++];
            if constexpr (move) {
                new (other_curr) Node(std::move(*curr));
            } else {
                new (other_curr) Node(*curr);
            }

            // insert into new hash table
            size_t hash = other_curr->hash();
            size_t step = (hash % prime) | 1;
            size_t idx = util::mod2(hash, table_size);
            Node* lookup = table[idx];
            while (lookup != nullptr) {
                idx = util::mod2(idx + step, table_size);
                lookup = table[idx];
            }
            table[idx] = other_curr;

            // link to previous node in array
            if (curr == this->head) {
                new_head = other_curr;
            } else {
                Node::join(new_tail, other_curr);
            }
            new_tail = other_curr;
            curr = next;
        }

        // return head/tail pointers for new list
        return std::make_pair(new_head, new_tail);
    }

    /* Allocate a new array of a given size and transfer the contents of the list. */
    void resize(size_t new_capacity) {
        table_size = new_capacity * 2;

        // allocate new array
        Node* new_array = Base::malloc_nodes(new_capacity);
        Node** new_table = calloc_table(table_size);
        if constexpr (DEBUG) {
            std::cout << "    -> allocate: " << new_capacity << " nodes" << std::endl;
        }

        // move nodes into new array
        std::pair<Node*, Node*> bounds(transfer<true>(new_array, new_table));
        this->head = bounds.first;
        this->tail = bounds.second;

        // replace arrays
        free(array);
        free(table);
        if constexpr (DEBUG) {
            std::cout << "    -> deallocate: " << this->capacity << " nodes" << std::endl;
        }
        array = new_array;
        table = new_table;
        free_list.first = nullptr;
        free_list.second = nullptr;
        this->capacity = new_capacity;
        tombstones = 0;
        exponent = util::log2(table_size);
        prime = PRIMES[exponent];
    }

    /* Look up a value in the hash table by providing an explicit hash/value. */
    inline Node* _search(const size_t hash, const Value& value) const noexcept {
        // get index and step for double hashing
        size_t step = (hash % prime) | 1;
        size_t idx = util::mod2(hash, table_size);
        Node* lookup = table[idx];

        // handle collisions
        while (lookup != nullptr) {
            if (lookup != this->temp() && util::eq(lookup->value(), value)) {
                return lookup;  // match found
            }

            // advance to next bucket
            idx = util::mod2(idx + step, table_size);
            lookup = table[idx];
        }

        // value not found
        return nullptr;
    }

public:

    /* Create an allocator with an optional fixed size. */
    HashAllocator(
        std::optional<size_t> capacity,
        bool dynamic,
        PyObject* specialization
    ) : Base(init_capacity(capacity, dynamic), dynamic, specialization),
        array(Base::malloc_nodes(this->capacity)),
        free_list(std::make_pair(nullptr, nullptr)),
        table_size(util::next_power_of_two(this->capacity * 2)),
        tombstones(0),
        exponent(util::log2(table_size)),
        prime(PRIMES[exponent]),
        table(calloc_table(table_size))
    {}

    /* Copy constructor. */
    HashAllocator(const HashAllocator& other) :
        Base(other),
        array(Base::malloc_nodes(this->capacity)),
        free_list(std::make_pair(nullptr, nullptr)),
        table_size(other.table_size),
        tombstones(0),
        exponent(other.exponent),
        prime(other.prime),
        table(calloc_table(table_size))
    {
        if (this->occupied) {
            std::pair<Node*, Node*> bounds(other.template transfer<false>(array, table));
            this->head = bounds.first;
            this->tail = bounds.second;
        }
    }

    /* Move constructor. */
    HashAllocator(HashAllocator&& other) noexcept :
        Base(std::move(other)),
        array(other.array),
        free_list(std::move(other.free_list)),
        table_size(other.table_size),
        tombstones(other.tombstones),
        exponent(other.exponent),
        prime(other.prime),
        table(other.table)
    {
        other.array = nullptr;
        other.free_list.first = nullptr;
        other.free_list.second = nullptr;
        other.table_size = 0;
        other.tombstones = 0;
        other.exponent = 0;
        other.prime = 0;
        other.table = nullptr;
    }

    /* Copy assignment operator. */
    HashAllocator& operator=(const HashAllocator& other) {
        if (this == &other) return *this;  // check for self-assignment

        // invoke parent
        Base::operator=(other);

        // destroy array
        free_list.first = nullptr;
        free_list.second = nullptr;
        if (array != nullptr) {
            free(array);
            free(table);
        }

        // copy table metadata
        table_size = other.table_size;
        tombstones = 0;
        exponent = other.exponent;
        prime = other.prime;

        // copy array
        array = Base::malloc_nodes(this->capacity);
        table = calloc_table(table_size);
        if (this->occupied) {
            std::pair<Node*, Node*> bounds(other.template transfer<false>(array, table));
            this->head = bounds.first;
            this->tail = bounds.second;
        } else {
            this->head = nullptr;
            this->tail = nullptr;
        }
        return *this;
    }

    /* Move assignment operator. */
    HashAllocator& operator=(HashAllocator&& other) noexcept {
        if (this == &other) return *this;  // check for self-assignment

        // invoke parent
        Base::operator=(std::move(other));

        // destroy array
        if (array != nullptr) {
            free(array);
            free(table);
        }

        // transfer ownership
        array = other.array;
        free_list.first = other.free_list.first;
        free_list.second = other.free_list.second;
        table_size = other.table_size;
        tombstones = other.tombstones;
        exponent = other.exponent;
        prime = other.prime;
        table = other.table;

        // reset other
        other.array = nullptr;
        other.free_list.first = nullptr;
        other.free_list.second = nullptr;
        other.table_size = 0;
        other.tombstones = 0;
        other.exponent = 0;
        other.prime = 0;
        other.table = nullptr;
        return *this;
    }

    /* Destroy an allocator and release its resources. */
    ~HashAllocator() noexcept {
        if (this->occupied) {
            Base::destroy_list();  // calls destructors
        }
        if (array != nullptr) {
            free(array);
            free(table);
            if constexpr (DEBUG) {
                std::cout << "    -> deallocate: " << this->capacity << " nodes";
                std::cout << std::endl;
            }
        }
    }

    /* Construct a new node for the list. */
    template <bool exist_ok = false, typename... Args>
    Node* create(Args&&... args) {
        // allocate into temporary node
        Node* node = this->temp();
        Base::init_node(node, std::forward<Args>(args)...);

        // search hash table
        size_t hash = node->hash();
        size_t step = (hash % prime) | 1;  // double hashing
        size_t idx = util::mod2(hash, table_size);
        Node* lookup = table[idx];
        while (lookup != nullptr && lookup != node) {  // fill in tombstones
            // check for matching value
            if (util::eq(lookup->value(), node->value())) {
                if constexpr (exist_ok) {
                    if constexpr (NodeTraits<Node>::has_mapped) {
                        lookup->mapped(std::move(node->mapped()));
                    }
                    node->~Node();  // clean up temporary node
                    return lookup;
                } else {
                    node->~Node();
                    std::ostringstream msg;
                    msg << "duplicate key: " << util::repr(node->value());
                    throw std::invalid_argument(msg.str());
                }
            }

            // advance to next index
            idx = util::mod2(idx + step, table_size);
            lookup = table[idx];
        }

        // check if we need to grow the array
        if (this->occupied == this->capacity && free_list.first == nullptr) {
            if (this->frozen() || !this->dynamic()) {
                throw Base::cannot_grow();
            }
            resize(this->capacity * 2);

            // rehash node to get new index
            hash = node->hash();
            step = (hash % prime) | 1;
            idx = util::mod2(hash, table_size);
            lookup = table[idx];
            while (lookup != nullptr) {  // don't need to check for tombstones
                idx = util::mod2(idx + step, table_size);
                lookup = table[idx];
            }
        } else if (lookup == node) {  // clear tombstone
            --tombstones;
        }

        // move temp node into allocator array
        if (free_list.first != nullptr) {
            node = free_list.first;
            Node* next = node->next();
            try {
                new (node) Node(std::move(*this->temp()));
            } catch (...) {
                node->next(next);  // restore free list
                throw;  // propagate
            }
            free_list.first = next;
            if (free_list.first == nullptr) free_list.second = nullptr;
        } else {
            node = &array[this->occupied];
            new (node) Node(std::move(*this->temp()));
        }

        // insert finalized node into hash table
        ++this->occupied;
        table[idx] = node;
        return node;
    }

    /* Release a node from the set. */
    void recycle(Node* node) {
        // look up node in hash table
        size_t hash = node->hash();
        size_t step = (hash % prime) | 1;  // double hashing
        size_t idx = util::mod2(hash, table_size);
        Node* lookup = table[idx];

        // handle collisions
        while (lookup != nullptr) {
            if (lookup == node) {
                Base::recycle(node);  // invoke parent method

                // mark as tombstone
                table[idx] = this->temp();
                ++tombstones;

                // shrink array and clear out tombstones if necessary
                if (!this->shrink()) {
                    if (tombstones > this->capacity / 8) {  // clear out tombstones
                        if (!this->frozen()) { // full defragmentation
                            resize(this->capacity);
                            return;
                        } else {  // rehash table without touching allocator array
                            // TODO
                            throw std::runtime_error("not implemented");
                        }
                    }
                    if (free_list.first == nullptr) {
                        free_list.first = node;
                        free_list.second = node;
                    } else {
                        free_list.second->next(node);
                        free_list.second = node;
                    }
                }
                return;
            }

            // advance to next index
            idx = util::mod2(idx + step, table_size);
            lookup = table[idx];
        }

        // node not found
        std::ostringstream msg;
        msg << "key not found: " << util::repr(node->value());
        throw util::KeyError(msg.str());
    }

    /* Remove all elements from the list. */
    void clear() noexcept {
        // invoke parent method
        Base::clear();

        // reset hash table parameters and shrink to default capacity
        if (!this->frozen()) {
            exponent = 0;
            prime = PRIMES[0];
            tombstones = 0;
            this->capacity = DEFAULT_CAPACITY;
            free(array);
            free(table);
            array = Base::malloc_nodes(this->capacity);
            table = calloc_table(table_size);
        }
    }

    /* Get the total amount of dynamic memory being managed by this allocator. */
    inline size_t nbytes() const {
        // NOTE: hash table takes 8 bytes per node (2 pointers per node)
        return Base::nbytes() + table_size * sizeof(Node*);
    }

    /* Search for a node by its value directly. */
    inline Node* search(const Value& key) const {
        return _search(std::hash<Value>{}(key), key);
    }

    /* Search for a node by reusing a hash from another node. */
    template <typename N, std::enable_if_t<std::is_base_of_v<NodeTag, N>, int> = 0>
    inline Node* search(const N* node) const {
        if constexpr (NodeTraits<N>::has_hash) {
            return _search(node->hash(), node->value());
        } else {
            size_t hash = std::hash<typename N::Value>{}(node->value());
            return _search(hash, node->value());
        }
    }

};


/* A custom allocator that directly hashes the node array to avoid auxiliary data
structures.  Uses a modified hopscotch strategy to resolve collisions.

Hopscotch hashing typically stores extra information in each bucket listing the
distance to the next node in the collision chain.  When a collision is encountered, we
skip through the collision chain using these offsets, checking only those nodes that
actually collide.  This reduces the amount of time spent probing irrelevant buckets,
and eliminates the need for tombstones.  In exchange, the collision chain is confined
to a finite neighborhood around the origin node (as set by the hop information),
possibly resulting in less predictable resizes than with other open addressing schemes.

Because of the direct integration with the allocator array, this approach does not
require an auxiliary data structure.  Instead, it uses only 2 extra bytes per node
to store the hopscotch offsets, making it more memory-efficient than the double hashing
scheme listed above, which must allocate a whole pointer (5-8 bytes) for each node.
The hopscotch algorithm also remains efficient at high load factors (up to 87.5%),
giving similar memory characteristics to an equivalent ListAllocator.

Because of the requirement that node addresses remain physically stable over their
lifetime, it is not possible to rearrange elements within the array.  This means that
the full hopscotch algorithm cannot be implemented as described in the original paper,
since it attempts to consolidate elements to improve cache locality.  Instead,
insertions into this map devolve into a linear search for an empty bucket, which limits
the potential of the hopscotch algorithm.  As a result, insertions may be slightly
slower than average, though searches and removals should remain fast at all times. */
template <typename NodeType>
class HashAllocator<NodeType, Collision::HOPSCOTCH> :
    public BaseAllocator<HashAllocator<NodeType, Collision::HOPSCOTCH>, NodeType>
{
    using Base = BaseAllocator<HashAllocator<NodeType, Collision::HOPSCOTCH>, NodeType>;
    friend Base;
    friend typename Base::MemGuard;

public:
    using Node = NodeType;
    using MemGuard = typename Base::MemGuard;
    static constexpr size_t DEFAULT_CAPACITY = 8;  // minimum table size

private:
    using Value = typename Node::Value;
    static constexpr unsigned char EMPTY = 255;
    static constexpr unsigned char NEIGHBORHOOD = 32;  // max probe distance
    static_assert(
        NEIGHBORHOOD < EMPTY,
        "neighborhood size must leave room for EMPTY flag"
    );

    /* A bucket storing offsets for the hopscotch algorithm.  Rather than using a
    bitmap, this approach stores two 8-bit offsets per bucket.  The first represents
    the distance to the head of this bucket's collision chain, and the second represents
    the distance to the next bucket in the current occupant's collision chain. */
    struct Bucket {
        unsigned char collisions = EMPTY;
        unsigned char next = EMPTY;
        alignas(Node) unsigned char data[sizeof(Node)];  // uninitialized payload

        /* NOTE: setting displacement=EMPTY indicates that the bucket does not have any
         * collisions.  Otherwise, it is the distance from the current bucket (origin)
         * to the first bucket in its collision chain.  If another value hashes to a
         * bucket that has an EMPTY displacement, then it is guaranteed to be unique.
         *
         * Setting next=EMPTY indicates that the current bucket is not occupied.
         * Otherwise, it is the distance to the next bucket in the chain.  If it is set
         * to 0, then it indicates that the current bucket is at the end of its
         * collision chain.
         */

        /* Get a pointer to the node within the bucket. */
        inline Node* node() noexcept {
            return reinterpret_cast<Node*>(&data);
        }

        /* Construct the node within the bucket. */
        template <typename... Args>
        inline void construct(Args&&... args) {
            new (reinterpret_cast<Node*>(&data)) Node(std::forward<Args>(args)...);
            // don't forget to set collisions and/or next!
        }

        /* Destroy the node within the bucket. */
        inline void destroy() noexcept {
            reinterpret_cast<Node&>(data).~Node();
            next = EMPTY;
        }

        /* Check if the bucket is empty. */
        inline bool occupied() const noexcept {
            return next != EMPTY;
        }

        /* A fast iterator over the bucket's neighborhood, following the offsets
        between each colliding node. */
        struct HopIterator {
            Bucket* table;
            size_t idx;
            size_t modulo;
            Bucket* curr;

            /* Iterator traits. */
            using iterator_category         = std::forward_iterator_tag;
            using value_type                = Bucket;
            using difference_type           = std::ptrdiff_t;
            using pointer                   = Bucket*;
            using reference                 = Bucket&;

            /* Construct a new iterator from the given starting bucket. */
            HopIterator(Bucket* table, size_t idx, size_t modulo) :
                table(table), idx(idx), modulo(modulo), curr(table[idx])
            {}

            /* Dereference the iterator to get the current bucket. */
            inline Bucket* operator*() const noexcept {
                return curr;
            }

            /* Advance the iterator to the next bucket in the chain. */
            inline HopIterator& operator++() noexcept {
                idx = (idx + curr.next) & modulo;
                curr = table[idx];
                return *this;
            }

            /* Check if the iterator has reached the end of the chain. */
            inline bool operator!=(const HopIterator& other) const noexcept {
                return !curr.next;
            }

            /* Get an iterator to the beginning of the chain. */
            inline HopIterator begin() const noexcept {
                return HopIterator(table, idx, modulo);
            }

            /* Get an iterator to the end of the chain. */
            inline HopIterator end() const noexcept {
                return HopIterator(table, idx, modulo);  // self-terminating
            }

        };

        /* Iterate through the bucket's collision chain, assuming it has one. */
        inline HopIterator chain(
            Bucket* table,
            size_t origin_idx,
            size_t modulo
        ) noexcept {
            return HopIterator(table, (origin_idx + collisions) & modulo, modulo);
        }

    };

    Bucket* table;  // dynamic array of buckets
    size_t modulo;  // bitmask for fast modulo arithmetic
    size_t max_load;  // maximum number of occupied buckets before resize

    /* Adjust the starting capacity of a set to a power of two. */
    inline static size_t init_capacity(std::optional<size_t> capacity) {
        if (!capacity.has_value()) {
            return DEFAULT_CAPACITY;
        }

        // round up to next power of two
        size_t result = capacity.value();
        return result < DEFAULT_CAPACITY ?
            DEFAULT_CAPACITY :
            util::next_power_of_two(result);
    }

    /* Look up a value in the hash table by providing an explicit hash/value. */
    Node* _search(const size_t hash, const Value& value) const {
        // TODO: check for specialization?
        // if constexpr (util::is_pyobject<Value>) {
        //     if (this->specialization != nullptr) {
        //         int comp = PyObject_IsInstance(value, this->specialization);
        //         if (comp == -1) throw util::catch_python();
        //         if (!comp) return nullptr;  // value cannot be contained in table
        //     }
        // }

        // identify starting bucket
        size_t idx = hash & modulo;
        Bucket* bucket = table + idx;

        // if collision chain is empty, then no match is possible
        if (bucket->collisions != EMPTY) {
            if (bucket->collisions) {  // advance to head of chain
                idx = (idx + bucket->collisions) & modulo;
                bucket = table + idx;
            }
            while (true) {
                if (util::eq(value, bucket->node()->value())) {
                    return bucket->node();
                }

                // advance to next bucket
                if (!bucket->next) break;  // end of chain
                idx += bucket->next;
                idx &= modulo;
                bucket = table + idx;
            }
        }

        // value not found
        return nullptr;
    }

    /* Copy/move the nodes from this allocator into another table. */
    template <bool move>
    std::pair<Node*, Node*> transfer(Bucket* other, size_t size) const {
        Node* new_head = nullptr;
        Node* new_tail = nullptr;
        size_t modulo = size - 1;

        // move nodes into new table
        Node* curr_node = this->head;
        while (curr_node != nullptr) {
            // rehash node
            size_t hash;
            if constexpr (NodeTraits<Node>::has_hash) {
                hash = curr_node->hash();
            } else {
                hash = std::hash<Value>{}(curr_node->value());
            }

            // get origin bucket in new array
            size_t origin_idx = hash & modulo;
            Bucket* origin = other + origin_idx;

            // linear probe starting from origin
            Bucket* bucket = origin;
            Bucket* prev = origin;  // previous bucket in chain
            unsigned char prev_distance = 0;  // distance from origin to prev
            unsigned char distance = 0;  // current probe length
            unsigned char next = origin->collisions;  // distance to next bucket in chain
            while (bucket->occupied()) {
                if (++distance == NEIGHBORHOOD) {
                    throw std::runtime_error("exceeded maximum probe length");
                }
                bucket = other + ((origin_idx + distance) & modulo);
                if (distance == next) {
                    prev = bucket;
                    prev_distance = distance;
                    next += bucket->next;
                }
            };

            // update hop information
            if (distance < origin->collisions) {  // bucket is new head of chain
                bucket->next = (origin->collisions != EMPTY) * (origin->collisions - distance);
                origin->collisions = (origin->collisions != 0) * distance;
            } else {  // bucket is in middle or end of chain
                unsigned char delta = distance - prev_distance;
                bucket->next = (prev->next != 0) * (prev->next - delta);
                prev->next = delta;
            }

            // transfer node into new array
            Node* next_node = curr_node->next();
            if constexpr (move) {
                bucket->construct(std::move(*curr_node));
            } else {
                bucket->construct(*curr_node);
            }

            // join with previous node and update head/tail pointers
            if (curr_node == this->head) {
                new_head = bucket->node();
            } else {
                Node::join(new_tail, bucket->node());
            }
            new_tail = bucket->node();
            curr_node = next_node;
        }

        // return head/tail pointers for new list
        return std::make_pair(new_head, new_tail);
    }

    /* Allocate a new table of a given size and transfer the contents of the list. */
    void resize(size_t new_capacity) {
        // allocate new table
        Bucket* new_table = new Bucket[new_capacity];
        if constexpr (DEBUG) {
            std::cout << "    -> allocate: " << new_capacity << " nodes" << std::endl;
        }

        // move nodes into new table
        std::pair<Node*, Node*> bounds(transfer<true>(new_table, new_capacity));
        this->head = bounds.first;
        this->tail = bounds.second;

        // replace table
        free(table);
        if constexpr (DEBUG) {
            std::cout << "    -> deallocate: " << this->capacity << " nodes";
            std::cout << std::endl;
        }
        this->capacity = new_capacity;
        table = new_table;
        modulo = new_capacity - 1;
        max_load = new_capacity - (new_capacity / 4);
    }

public:

    /* Create an allocator with an optional fixed size. */
    HashAllocator(
        std::optional<size_t> capacity,
        bool dynamic,
        PyObject* specialization
    ) : Base(init_capacity(capacity), dynamic, specialization),
        table(new Bucket[this->capacity]), modulo(this->capacity - 1),
        max_load(this->capacity - (this->capacity / 4))
    {}

    /* Copy constructor. */
    HashAllocator(const HashAllocator& other) :
        Base(other), table(new Bucket[this->capacity]), modulo(other.modulo),
        max_load(other.max_load)
    {
        // copy over existing nodes in correct list order (head -> tail)
        if (this->occupied) {
            std::pair<Node*, Node*> bounds(
                other.template transfer<false>(table, this->capacity)
            );
            this->head = bounds.first;
            this->tail = bounds.second;
        }
    }

    /* Move constructor. */
    HashAllocator(HashAllocator&& other) noexcept :
        Base(std::move(other)), table(other.table), modulo(other.modulo),
        max_load(other.max_load)
    {
        other.table = nullptr;
    }

    /* Copy assignment operator. */
    HashAllocator& operator=(const HashAllocator& other) {
        if (this == &other) return *this;  // check for self-assignment

        // invoke parent
        Base::operator=(other);

        // destroy table
        if (table != nullptr) {
            free(table);
        }

        // copy table
        table = new Bucket[this->capacity];
        modulo = other.modulo;
        max_load = other.max_load;
        if (this->occupied) {
            std::pair<Node*, Node*> bounds(
                other.template transfer<false>(table, this->capacity)
            );
            this->head = bounds.first;
            this->tail = bounds.second;
        } else {
            this->head = nullptr;
            this->tail = nullptr;
        }
        return *this;
    }

    /* Move assignment operator. */
    HashAllocator& operator=(HashAllocator&& other) noexcept {
        if (this == &other) return *this;  // check for self-assignment

        // invoke parent
        Base::operator=(std::move(other));

        // destroy table
        if (table != nullptr) {
            free(table);
        }

        // transfer ownership
        table = other.table;
        modulo = other.modulo;
        max_load = other.max_load;
        other.table = nullptr;
        return *this;
    }

    /* Destroy an allocator and release its resources. */
    ~HashAllocator() noexcept {
        if (this->head != nullptr) {
            Base::destroy_list();  // calls destructors
        }
        if (table != nullptr) {
            free(table);
            if constexpr (DEBUG) {
                std::cout << "    -> deallocate: " << this->capacity << " nodes";
                std::cout << std::endl;
            }
        }
    }

    /* Construct a new node from the table. */
    template <bool exist_ok = false, typename... Args>
    Node* create(Args&&... args) {
        // allocate into temporary node
        Node* node = this->temp();
        Base::init_node(node, std::forward<Args>(args)...);

        // check if we need to grow the array
        if (this->occupied >= max_load) resize(this->capacity * 2);

        // search hash table to get origin bucket
        size_t origin_idx = node->hash() & modulo;
        Bucket* origin = table + origin_idx;

        // if bucket has collisions, search the chain for a matching value
        if (origin->collisions != EMPTY) {
            size_t idx = (origin_idx + origin->collisions) & modulo;
            Bucket* bucket = table + idx;
            while (true) {
                // check for value match
                if (util::eq(bucket->node()->value(), node->value())) {
                    if constexpr (exist_ok) {
                        if constexpr (NodeTraits<Node>::has_mapped) {
                            bucket->node()->mapped(std::move(node->mapped()));
                        }
                        node->~Node();  // clean up temporary node
                        return bucket->node();
                    } else {
                        std::ostringstream msg;
                        msg << "duplicate key: " << util::repr(node->value());
                        node->~Node();
                        throw util::KeyError(msg.str());
                    }
                }

                // advance to next bucket
                if (!(bucket->next)) break;  // end of chain
                idx += bucket->next;
                idx &= modulo;
                bucket = table + idx;
            }
        }

        // NOTE: if we get here, then we know the value is unique and must be inserted
        // into the hash table.  This requires a linear probe over the hop neighborhood
        // as well as careful updates to the hop information for the collision chain.

        // linear probe starting from origin
        Bucket* bucket = origin;
        Bucket* prev = origin;  // previous bucket in chain
        unsigned char prev_distance = 0;  // distance from origin to prev
        unsigned char distance = 0;  // current probe length
        unsigned char next = origin->collisions;  // distance to next bucket in chain
        while (bucket->occupied()) {
            if (++distance == NEIGHBORHOOD) {  // exceeded maximum probe length
                resize(this->capacity * 2);  // grow table
                return create(std::move(*node));  // retry
            }
            bucket = table + ((origin_idx + distance) & modulo);
            if (distance == next) {
                prev = bucket;
                prev_distance = distance;
                next += bucket->next;
            }
        }

        // update hop information
        if (distance < origin->collisions) {  // bucket is new head of chain
            bucket->next = (origin->collisions != EMPTY) * (origin->collisions - distance);
            origin->collisions = (origin->collisions != 0) * distance;
        } else {  // bucket is in middle or end of chain
            unsigned char delta = distance - prev_distance;
            bucket->next = (prev->next != 0) * (prev->next - delta);
            prev->next = delta;
        }

        // insert node into empty bucket
        bucket->construct(std::move(*node));
        ++this->occupied;
        return bucket->node();
    }

    /* Release a node from the table. */
    void recycle(Node* node) {
        // look up origin node
        size_t idx = node->hash() & modulo;
        Bucket* origin = table + idx;

        // trivial case: bucket does not have a collision chain
        if (origin->collisions == EMPTY) {
            if (origin->occupied() && util::eq(origin->node()->value(), node->value())) {
                origin->destroy();
                --this->occupied;
                return;
            }
            std::ostringstream msg;
            msg << "key not found: " << util::repr(node->value());
            throw util::KeyError(msg.str());
        }

        // follow collision chain
        Bucket* prev = origin;
        idx = (idx + origin->collisions) & modulo;
        Bucket* bucket = table + idx;
        while (true) {
            // check for matching value
            if (util::eq(bucket->node()->value(), node->value())) {
                // update hop information
                unsigned char has_next = (bucket->next > 0);
                if (prev == origin) {  // bucket is head of collision chain
                    origin->collisions = has_next ?
                        (origin->collisions + bucket->next) :
                        EMPTY;
                } else {  // bucket is in middle or end of collision chain
                    prev->next = has_next * (prev->next + bucket->next);
                }

                // destroy node
                if constexpr (DEBUG) {
                    std::cout << "    -> recycle: " << util::repr(node->value());
                    std::cout << std::endl;
                }
                bucket->destroy();
                --this->occupied;
                this->shrink();
                return;
            }

            // advance to next bucket
            if (!prev->next) break;  // end of chain
            idx = (idx + prev->next) & modulo;
            prev = bucket;
            bucket = table + idx;
        }

        // node not found
        std::ostringstream msg;
        msg << "key not found: " << util::repr(node->value());
        throw util::KeyError(msg.str());
    }

    /* Remove all elements from the table. */
    void clear() noexcept {
        Base::clear();

        // reset hash table and shrink to default capacity
        if (!this->frozen()) {
            this->capacity = DEFAULT_CAPACITY;
            free(table);
            table = new Bucket[this->capacity];
            modulo = this->capacity - 1;
            max_load = this->capacity - (this->capacity / 8);
        }
    }

    /* Get the total amount of dynamic memory being managed by this allocator. */
    inline size_t nbytes() const {
        // NOTE: hop information takes 2 extra bytes per bucket
        return this->capacity * sizeof(Bucket);
    }

    /* Search for a node by its value directly. */
    inline Node* search(const Value& key) const {
        return _search(std::hash<Value>{}(key), key);
    }

    /* Search for a node by reusing a hash from another node. */
    template <typename N, std::enable_if_t<std::is_base_of_v<NodeTag, N>, int> = 0>
    inline Node* search(const N* node) const {
        if constexpr (NodeTraits<N>::has_hash) {
            return _search(node->hash(), node->value());
        } else {
            size_t hash = std::hash<typename N::Value>{}(node->value());
            return _search(hash, node->value());
        }
    }

};


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_CORE_ALLOCATE_H
