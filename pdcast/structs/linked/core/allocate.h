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
leaks.  This is a lot less elegant than using a logging library, but it gets the job
done, avoids a dependency, and is easier to use from a Python REPL. */
constexpr bool DEBUG = false;


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
        STRICTLY_TYPED          = 0b100  // (currently unused)
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
        // variadic dispatch to node constructor
        new (node) Node(std::forward<Args>(args)...);

        // check python specialization if enabled
        if (specialization != nullptr && !node->typecheck(specialization)) {
            std::ostringstream msg;
            msg << util::repr(node->value()) << " is not of type ";
            msg << util::repr(specialization);
            node->~Node();
            throw util::TypeError(msg.str());
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
            curr->~Node();
            curr = next;
        }
    }

    /* Throw an error indicating that the allocator is frozen at its current size. */
    inline auto cannot_grow() const {
        const Derived* self = static_cast<const Derived*>(this);
        std::ostringstream msg;
        msg << "allocator is frozen at size " << self->max_size().value_or(capacity);
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

    /* Resize the allocator to store a specific number of nodes. */
    void reserve(size_t new_size) {
        // ensure new capacity is large enough to store all existing nodes
        if (new_size < occupied) {
            throw std::invalid_argument(
                "new capacity cannot be smaller than current size"
            );
        }
    }

    /////////////////////////
    ////    INHERITED    ////
    /////////////////////////

    /* Attempt to resize the allocator based on an optional size. */
    inline MemGuard try_reserve(std::optional<size_t> new_size) {
        if (!new_size.has_value()) {
            return MemGuard();
        }
        Derived* self = static_cast<Derived*>(this);
        return self->reserve(new_size.value());
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
        Derived* self = static_cast<Derived*>(this);
        return self->reserve(occupied + length.value());
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

    /* Get the maximum number of elements that this allocator can support if it does
    not support dynamic sizing. */
    inline std::optional<size_t> max_size() const noexcept {
        return dynamic() ? std::nullopt : std::make_optional(capacity);
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


/* A custom allocator that uses a dynamic array to manage memory for nodes within a
linked list.

Most linked list implementations (including `std::list`) typically allocate each node
individually on the heap, rather than placing them in a contiguous array like
`std::vector`.  This can lead to fragmentation, which degrades cache performance and
adds overhead to every insertion/removal.  The reasons for doing this are sound: linked
data structures require the memory address of each of their nodes to remain stable over
time, in order to maintain the integrity of their internal pointers.  Individual heap
allocations, while not the most efficient option, are one way to guarantee this
stability.

This allocator, on the other hand, uses a similar strategy to `std::vector`, placing
all nodes in a single contiguous array which grows and shrinks as needed.  This
eliminates fragmentation and, by growing the array geometrically, amortizes the cost of
reallocation to further minimize heap allocations.  The only downsides are that each
resize operation is O(n), and we always overallocate some amount of memory to ensure
that we don't need to resize too often.

Note that since each node maintains a reference to at least one other node in the list,
we still need to ensure that their physical addresses do not change over time.  This
means we are prohibited from moving nodes within the array, as doing so would
compromise the list's integrity.  As a result, holes can form within the allocator
array as elements are removed from the list.  Luckily, since the nodes may be linked,
we can use them to form a singly-linked free list that tracks the location of each
hole, without requiring any additional data structures.

Filling holes in this way can lead to a secondary form of fragmentation, where the
order of the linked list no longer matches the order of the nodes within the array.
This forces the memory subsystem to load and unload individual cache lines more
frequently, degrading performance.  To mitigate this, whenever we reallocate the array,
we copy the nodes into the new array in the same order as they appear in the list.
This ensures that each node immediately succeeds the previous node in memory,
minimizing the number of cache lines that need to be loaded to traverse the list. */
template <typename NodeType>
class ListAllocator : public BaseAllocator<ListAllocator<NodeType>, NodeType> {
    using Base = BaseAllocator<ListAllocator<NodeType>, NodeType>;
    friend Base;
    friend typename Base::MemGuard;

public:
    using Node = typename Base::Node;
    using MemGuard = typename Base::MemGuard;
    static constexpr size_t DEFAULT_CAPACITY = 8;  // minimum array size

private:
    Node* array;  // dynamic array of nodes
    std::pair<Node*, Node*> free_list;  // singly-linked list of open nodes

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
        auto [head, tail] = transfer<true>(new_array);
        this->head = head;
        this->tail = tail;

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

    /* Shrink a dynamic allocator if it is under the minimum load factor.  This is
    called automatically by recycle() as well as when a MemGuard falls out of scope,
    guaranteeing the load factor is never less than 25% of the list's capacity. */
    inline bool shrink() {
        if (!this->frozen() &&
            this->dynamic() &&
            this->capacity != DEFAULT_CAPACITY &&
            this->occupied <= this->capacity / 4
        ) {
            size_t size = util::next_power_of_two(this->occupied * 2);
            resize(size < DEFAULT_CAPACITY ? DEFAULT_CAPACITY : size);
            return true;
        }
        return false;
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
            auto [head, tail] = other.template transfer<false>(array);
            this->head = head;
            this->tail = tail;
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
            auto [head, tail] = other.template transfer<false>(array);
            this->head = head;
            this->tail = tail;
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
        Base::clear();

        // reset free list and shrink to default capacity
        free_list.first = nullptr;
        free_list.second = nullptr;
        if (!this->frozen() && this->dynamic() && this->capacity != DEFAULT_CAPACITY) {
            this->capacity = DEFAULT_CAPACITY;
            free(array);
            if constexpr (DEBUG) {
                std::cout << "    -> deallocate: " << this->capacity << " nodes";
                std::cout << std::endl;
            }
            array = Base::malloc_nodes(this->capacity);
            if constexpr (DEBUG) {
                std::cout << "    -> allocate: " << this->capacity << " nodes";
                std::cout << std::endl;
            }
        }
    }

    /* Resize the allocator to store a specific number of nodes. */
    MemGuard reserve(size_t new_size) {
        Base::reserve(new_size);

        // if frozen, check new size against current capacity
        if (this->frozen() || !this->dynamic()) {
            if (new_size > this->capacity) {
                throw Base::cannot_grow();
            } else {
                return MemGuard();  // empty guard
            }
        }

        // otherwise, grow the array if necessary
        size_t new_capacity = util::next_power_of_two(new_size);
        if (new_capacity > this->capacity) {
            resize(new_capacity);
        }

        // freeze allocator until guard falls out of scope
        return MemGuard(this);
    }

};


//////////////////////////////
////    SET/DICTIONARY    ////
//////////////////////////////


/* A custom allocator that directly hashes the node array to allow for constant-time
lookups.  Uses a modified hopscotch strategy to resolve collisions.

Hopscotch hashing typically stores extra information in each bucket listing the
distance to the next node in the collision chain.  When a collision is encountered, we
skip through the collision chain using these offsets, checking only those nodes that
actually collide.  This reduces the amount of time spent probing irrelevant buckets,
and eliminates the need for tombstones.  In exchange, the collision chain is confined
to a finite neighborhood around the origin node (as set by the hop information).

Because of the direct integration with the allocator array, this approach does not
require any auxiliary data structures.  Instead, it uses 2 extra bytes per node to
store the hopscotch offsets, which can be packed into the allocator array for maximum
efficiency.  However, due to the requirement that node addresses remain physically
stable over their lifetime, it is not possible to rearrange elements within the array
as we insert items.  This means that the full hopscotch algorithm cannot be implemented
as described in the original paper, since it attempts to consolidate elements to
improve cache locality.  Instead, insertions into this map devolve into a linear search
for an empty bucket, which limits the potential of the hopscotch algorithm.  As a
result, insertions have comparable performance to a typical linear probing algorithm,
but searches and removals will skip through the neighborhood like normal.

The benefits of this algorithm are as follows.  First, it avoids the need for
tombstones, which can accumulate within the list and degrade performance.  Second, it
allows searches to work well at high load factors, since we only need to check a
well-defined collision chain for a conflict, and third, it does not require us to move
any elements within the array.  These properties allow us to implement a set of linked
nodes with minimal overhead, while still maintaining as much performance as possible. */
template <typename NodeType, bool packed = false>
class HashAllocator : public BaseAllocator<HashAllocator<NodeType>, NodeType> {
    using Base = BaseAllocator<HashAllocator<NodeType>, NodeType>;
    friend Base;
    friend typename Base::MemGuard;

public:
    using Node = NodeType;
    using MemGuard = typename Base::MemGuard;
    static constexpr size_t DEFAULT_CAPACITY = 8;  // minimum table size

    /* An enum containing compile-time flags to control the behavior of the create()
    and recycle() factory methods in an optimized fashion. */
    enum : unsigned int {
        DEFAULT = 0,
        EXIST_OK = 1 << 0,
        NOEXIST_OK = 1 << 1,
        REPLACE_MAPPED = 1 << 2,
        RETURN_MAPPED = 1 << 3,
        UNLINK = 1 << 4,
        EVICT_HEAD = 1 << 5,
        EVICT_TAIL = 1 << 6,
        INSERT_HEAD = 1 << 7,
        INSERT_TAIL = 1 << 8,
        MOVE_HEAD = 1 << 9,
        MOVE_TAIL = 1 << 10,
    };

private:
    using Value = typename Node::Value;
    static constexpr unsigned char EMPTY = 255;
    static constexpr unsigned char MAX_PROBE_LENGTH = 255;
    static_assert(
        MAX_PROBE_LENGTH <= EMPTY,
        "neighborhood size must leave room for EMPTY flag"
    );

    /* NOTE: bucket types are hidden behind template specializations to allow for both
     * packed and unpacked representations.  Both are identical, but the packed
     * representation is more space efficient.  It can, however, degrade performance on
     * some systems due to unaligned memory accesses.  The unpacked representation is
     * more performant and portable, but always wastes between 2 and 6 extra bytes per
     * bucket.
     *
     * NOTE: setting displacement=EMPTY indicates that the bucket does not have any
     * collisions.  Otherwise, it is the distance from the current bucket (origin) to
     * the first bucket in its collision chain.  If another value hashes to a bucket
     * that has an EMPTY displacement, then it is guaranteed to be unique.
     *
     * Setting next=EMPTY indicates that the current bucket is not occupied.  Otherwise,
     * it is the distance to the next bucket in the chain.  If it is set to 0, then it
     * indicates that the current bucket is at the end of its collision chain.
     *
     * NOTE: due to the way the hopscotch algorithm works, each node is assigned to a
     * finite neighborhood of size MAX_PROBE_LENGTH.  It is possible (albeit very rare)
     * that during insertion, a linear probe can surpass this length, which causes the
     * algorithm to fail.  The probability of this is extremely low (impossible for
     * sets under 255 elements, otherwise order 10**-29 for MAX_PROBE_LENGTH=255 at 75%
     * maximum load), but is still possible, with increasing (but still very small)
     * likelihood as the container size increases and/or probe length shortens.
     * Dynamic sets can work around this by simply growing to a larger table size, but
     * for fixed-size sets, it is a fatal error.
     */

    template <bool pack = false, typename Dummy = void>
    struct BucketType {
        struct Bucket {
            unsigned char collisions = EMPTY;
            unsigned char next = EMPTY;
            alignas(Node) unsigned char data[sizeof(Node)];  // uninitialized payload

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

        };
    };

    template <typename Dummy>
    struct BucketType<true, Dummy> {
        #pragma pack(push, 1)
        struct Bucket {
            unsigned char collisions = EMPTY;
            unsigned char next = EMPTY;
            alignas(Node) unsigned char data[sizeof(Node)];  // uninitialized payload

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

        };
        #pragma pack(pop)
    };

    /* A bucket storing offsets for the hopscotch algorithm.  Rather than using a
    bitmap, this approach stores two 8-bit offsets per bucket.  The first represents
    the distance to the head of this bucket's collision chain, and the second represents
    the distance to the next bucket in the current occupant's collision chain. */
    using Bucket = typename BucketType<packed>::Bucket;

    Bucket* table;  // dynamic array of buckets
    size_t modulo;  // bitmask for fast modulo arithmetic
    size_t max_occupants;  // (for fixed-size sets) maximum number of occupants

    /* Adjust the starting capacity of a set to a power of two. */
    inline static size_t init_capacity(std::optional<size_t> capacity, bool dynamic) {
        if (!capacity.has_value()) {
            return DEFAULT_CAPACITY;
        }

        // round up to next power of two
        size_t result = capacity.value();
        result = util::next_power_of_two(result + (result / 3));
        return result < DEFAULT_CAPACITY ? DEFAULT_CAPACITY : result;
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
            Bucket* prev = nullptr;
            Bucket* bucket = origin;
            unsigned char prev_distance = 0;  // distance from origin to prev
            unsigned char distance = 0;  // current probe length
            unsigned char next = origin->collisions;  // distance to next bucket in chain
            while (bucket->occupied()) {
                if (distance == next) {
                    prev = bucket;
                    prev_distance = distance;
                    next += bucket->next;
                }
                if (++distance == MAX_PROBE_LENGTH) {
                    throw std::runtime_error("exceeded maximum probe length");
                }
                bucket = other + ((origin_idx + distance) & modulo);
            };

            // update hop information
            if (prev == nullptr) {  // bucket is new head of chain
                bucket->next = (origin->collisions != EMPTY) * (origin->collisions - distance);
                origin->collisions = distance;
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
        try {
            auto [head, tail] = transfer<true>(new_table, new_capacity);
            this->head = head;
            this->tail = tail;
        } catch (...) {  // exceeded maximum probe length
            delete[] new_table;
            if (!this->frozen() && this->dynamic()) {
                resize(new_capacity * 2);  // retry with a larger table
                return;
            } else {
                throw;  // propagate error
            }
            resize(new_capacity * 2);
            return;
        }

        // replace table
        free(table);
        if constexpr (DEBUG) {
            std::cout << "    -> deallocate: " << this->capacity << " nodes";
            std::cout << std::endl;
        }
        this->capacity = new_capacity;
        table = new_table;
        modulo = new_capacity - 1;
    }

    /* Shrink a dynamic allocator if it is under the minimum load factor.  This is
    called automatically by recycle() as well as when a MemGuard falls out of scope,
    guaranteeing the load factor is never less than 25% of the list's capacity. */
    inline bool shrink() {
        if (!this->frozen() &&
            this->dynamic() &&
            this->capacity != DEFAULT_CAPACITY &&
            this->occupied <= this->capacity / 4
        ) {
            size_t size = util::next_power_of_two(this->occupied + (this->occupied / 3));
            resize(size < DEFAULT_CAPACITY ? DEFAULT_CAPACITY : size);
            return true;
        }
        return false;
    }

    /* Move a node to the head of the list once it's been found */
    inline void move_to_head(Node* node) noexcept {
        if (node != this->head) {
            Node* prev;
            if constexpr (NodeTraits<Node>::has_prev) {
                prev = node->prev();
            } else {
                prev = nullptr;
                Node* curr = this->head;
                while (curr != node) {
                    prev = curr;
                    curr = curr->next();
                }
            }
            if (node == this->tail) this->tail = prev;
            Node::unlink(prev, node, node->next());
            Node::link(nullptr, node, this->head);
            this->head = node;
        }
    }

    /* Move a node to the tail of the list once it's been found */
    inline void move_to_tail(Node* node) noexcept {
        if (node != this->tail) {
            Node* prev;
            if constexpr (NodeTraits<Node>::has_prev) {
                prev = node->prev();
            } else {
                prev = nullptr;
                Node* curr = this->head;
                while (curr != node) {
                    prev = curr;
                    curr = curr->next();
                }
            }
            if (node == this->head) this->head = node->next();
            Node::unlink(prev, node, node->next());
            Node::link(this->tail, node, nullptr);
            this->tail = node;
        }
    }

    /* Evict the head node of the list. */
    inline void evict_head() {
        Node* evicted = this->head;
        this->head = evicted->next();
        if (this->head == nullptr) this->tail = nullptr;
        Node::unlink(nullptr, evicted, this->head);
        recycle(evicted);
    }

    /* Evict the tail node of the list. */
    inline void evict_tail() {
        Node* evicted = this->tail;
        Node* prev;
        if constexpr (NodeTraits<Node>::has_prev) {
            prev = evicted->prev();
        } else {
            prev = nullptr;
            Node* curr = this->head;
            while (curr != evicted) {
                prev = curr;
                curr = curr->next();
            }
        }
        this->tail = prev;
        if (this->tail == nullptr) this->head = nullptr;
        Node::unlink(prev, evicted, nullptr);
        recycle(evicted);
    }

    /* Insert a node at the head of the list. */
    inline void insert_head(Node* node) noexcept {
        Node::link(nullptr, node, this->head);
        this->head = node;
        if (this->tail == nullptr) this->tail = node;
    }

    /* Insert a node at the tail of the list. */
    inline void insert_tail(Node* node) noexcept {
        Node::link(this->tail, node, nullptr);
        this->tail = node;
        if (this->head == nullptr) this->head = node;
    }

    /* Unlink a node from its neighbors before recycling it. */
    inline void unlink(Node* node) {
        Node* next = node->next();
        Node* prev;
        if constexpr (NodeTraits<Node>::has_prev) {
            prev = node->prev();
        } else {
            prev = nullptr;
            Node* curr = this->head;
            while (curr != node) {
                prev = curr;
                curr = curr->next();
            }
        }
        if (node == this->head) this->head = next;
        if (node == this->tail) this->tail = prev;
        Node::unlink(prev, node, next);
    }

    /* Look up a value in the hash table by providing an explicit hash/value. */
    template <unsigned int flags = DEFAULT>
    Node* _search(const size_t hash, const Value& value) {
        static_assert(
            !((flags & MOVE_HEAD) && (flags & MOVE_TAIL)),
            "cannot move node to both head and tail of list"
        );

        // identify starting bucket
        size_t idx = hash & modulo;
        Bucket* bucket = table + idx;

        // if collision chain is empty, then no match is possible
        if (bucket->collisions != EMPTY) {
            if (bucket->collisions) {  // advance to head of chain
                idx += bucket->collisions;
                idx &= modulo;
                bucket = table + idx;
            }
            while (true) {
                Node* node = bucket->node();
                if (node->hash() == hash && util::eq(node->value(), value)) {
                    // move node if directed
                    if constexpr (flags & MOVE_HEAD) {
                        move_to_head(node);
                    } else if constexpr (flags & MOVE_TAIL) {
                        move_to_tail(node);
                    }
                    return node;
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

    /* Remove a value in the hash table by providing an explicit hash/value. */
    template <unsigned int flags = DEFAULT>
    void _recycle(const size_t hash, const Value& value) {
        // look up origin node
        size_t idx = hash & modulo;
        Bucket* origin = table + idx;

        // if collision chain is empty, then no match is possible
        if (origin->collisions != EMPTY) {
            // follow collision chain
            Bucket* prev = nullptr;
            Bucket* bucket = origin;
            if (origin->collisions) {
                idx += origin->collisions;
                idx &= modulo;
                bucket = table + idx;
            }
            while (true) {
                Node* node = bucket->node();
                if (node->hash() == hash && util::eq(node->value(), value)) {
                    // update hop information
                    unsigned char has_next = (bucket->next > 0);
                    if (prev == nullptr) {  // bucket is head of collision chain
                        origin->collisions = has_next ?
                            origin->collisions + bucket->next : EMPTY;
                    } else {  // bucket is in middle or end of collision chain
                        prev->next = has_next * (prev->next + bucket->next);
                    }

                    // unlink from neighbors if directed
                    if constexpr (flags & UNLINK) {
                        unlink(node);
                    }

                    // destroy node
                    if constexpr (DEBUG) {
                        std::cout << "    -> recycle: " << util::repr(value);
                        std::cout << std::endl;
                    }
                    bucket->destroy();
                    --this->occupied;
                    this->shrink();  // attempt to shrink table
                    return;
                }

                // advance to next bucket
                if (!bucket->next) break;  // end of chain
                idx += bucket->next;
                idx &= modulo;
                prev = bucket;
                bucket = table + idx;
            }
        }

        // node not found
        if constexpr (flags & NOEXIST_OK) {
            return;
        } else {
            std::ostringstream msg;
            msg << "key not found: " << util::repr(value);
            throw util::KeyError(msg.str());
        }
    }

public:

    /* Create an allocator with an optional fixed size. */
    HashAllocator(
        std::optional<size_t> capacity,
        bool dynamic,
        PyObject* specialization
    ) : Base(init_capacity(capacity, dynamic), dynamic, specialization),
        table(new Bucket[this->capacity]), modulo(this->capacity - 1),
        max_occupants(dynamic ? std::numeric_limits<size_t>::max() : capacity.value())
    {}

    /* Copy constructor. */
    HashAllocator(const HashAllocator& other) :
        Base(other), table(new Bucket[this->capacity]), modulo(other.modulo),
        max_occupants(other.max_occupants)
    {
        // copy over existing nodes in correct list order (head -> tail)
        if (this->occupied) {
            auto [head, tail] = other.template transfer<false>(table, this->capacity);
            this->head = head;
            this->tail = tail;
        }
    }

    /* Move constructor. */
    HashAllocator(HashAllocator&& other) noexcept :
        Base(std::move(other)), table(other.table), modulo(other.modulo),
        max_occupants(other.max_occupants)
    {
        other.table = nullptr;
    }

    /* Copy assignment operator. */
    HashAllocator& operator=(const HashAllocator& other) {
        if (this == &other) return *this;  // check for self-assignment

        // invoke parent
        Base::operator=(other);

        // destroy current table
        if (table != nullptr) free(table);

        // copy new table
        table = new Bucket[this->capacity];
        modulo = other.modulo;
        max_occupants = other.max_occupants;
        if (this->occupied) {
            auto [head, tail] = other.template transfer<false>(table, this->capacity);
            this->head = head;
            this->tail = tail;
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

        // destroy current table
        if (table != nullptr) free(table);

        // transfer ownership
        table = other.table;
        modulo = other.modulo;
        max_occupants = other.max_occupants;
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

    // TODO: things can probably get faster if we store the hash on the bucket rather
    // than the node.  This would allow us to allocate directly into the target node
    // rather than allocating into a temporary node and then searching from there.
    // We would simply extract the first value from the arg pack, hash it, and then
    // search for a node that matches its value.  If 

    /* Construct a new node from the table. */
    template <unsigned int flags = DEFAULT, typename... Args>
    Node* create(Args&&... args) {
        static_assert(
            !((flags & MOVE_HEAD) && (flags & MOVE_TAIL)),
            "cannot move node to both head and tail of list"
        );
        static_assert(
            !((flags & EVICT_HEAD) && (flags & EVICT_TAIL)),
            "cannot evict node from both head and tail of list"
        );
        static_assert(
            !((flags & INSERT_HEAD) && (flags & INSERT_TAIL)),
            "cannot insert node at both head and tail of list"
        );

        // allocate into temporary node
        Node* node = this->temp();
        Base::init_node(node, std::forward<Args>(args)...);

        // search hash table to get origin bucket
        size_t origin_idx = node->hash() & modulo;
        Bucket* origin = table + origin_idx;

        // if bucket has collisions, search the chain for a matching value
        if (origin->collisions != EMPTY) {
            size_t idx = origin_idx;
            Bucket* bucket = origin;
            if (origin->collisions) {  // advance to head of chain
                idx += bucket->collisions;
                idx &= modulo;
                bucket = table + idx;
            }
            while (true) {
                // check for value match
                if (bucket->node()->hash() == node->hash() &&
                    util::eq(bucket->node()->value(), node->value())
                ) {
                    if constexpr (flags & EXIST_OK) {
                        if constexpr (
                            NodeTraits<Node>::has_mapped &&
                            (flags & REPLACE_MAPPED)
                        ) {
                            bucket->node()->mapped(std::move(node->mapped()));
                        }
                        node->~Node();  // clean up temporary node
                        if constexpr (flags & MOVE_HEAD) {
                            move_to_head(bucket->node());
                        } else if constexpr (flags & MOVE_TAIL) {
                            move_to_tail(bucket->node());
                        }
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

        // check if we need to grow the array if it is dynamic
        if (this->dynamic()) {
            if (this->occupied >= this->capacity - (this->capacity / 4)) {
                if (this->frozen()) throw Base::cannot_grow();
                resize(this->capacity * 2);  // grow table
                origin_idx = node->hash() & modulo;  // rehash node
                origin = table + origin_idx;
            }

        // otherwise if set is of fixed size, either evict an element or throw an error
        } else if (this->occupied == this->max_occupants) {
            if constexpr (flags & EVICT_HEAD) {
                evict_head();
            } else if constexpr (flags & EVICT_TAIL) {
                evict_tail();
            } else {
                throw Base::cannot_grow();
            }
        }

        // linear probe starting from origin
        Bucket* prev = nullptr;
        Bucket* bucket = origin;
        unsigned char prev_distance = 0;  // distance from origin to prev
        unsigned char distance = 0;  // current probe length
        unsigned char next = origin->collisions;  // distance to next bucket in chain
        while (bucket->occupied()) {
            if (distance == next) {
                prev = bucket;
                prev_distance = distance;
                next += bucket->next;
            }
            if (++distance == MAX_PROBE_LENGTH) {
                if (!this->frozen() && this->dynamic()) {
                    resize(this->capacity * 2);  // grow table
                    return create(std::move(*node));  // retry
                } else {
                    throw std::runtime_error("exceeded maximum probe length");
                }
            }
            bucket = table + ((origin_idx + distance) & modulo);
        }

        // update hop information
        if (prev == nullptr) {  // bucket is new head of chain
            bucket->next = (origin->collisions != EMPTY) * (origin->collisions - distance);
            origin->collisions = distance;
        } else {  // bucket is in middle or end of chain
            unsigned char delta = distance - prev_distance;
            bucket->next = (prev->next != 0) * (prev->next - delta);
            prev->next = delta;
        }

        // insert node into empty bucket
        bucket->construct(std::move(*node));
        ++this->occupied;
        node = bucket->node();  // simplify syntax
        if constexpr (flags & INSERT_HEAD) {
            insert_head(node);
        } else if constexpr (flags & INSERT_TAIL) {
            insert_tail(node);
        }
        return node;
    }

    /* Release a node from the table. */
    template <unsigned int flags = DEFAULT>
    inline void recycle(Node* node) {
        _recycle<flags>(node->hash(), node->value());
    }

    /* Release a node from the table after looking up its value. */
    template <unsigned int flags = DEFAULT>
    inline void recycle(const Value& key) {
        _recycle<flags>(std::hash<Value>{}(key), key);
    }

    /* Remove all elements from the table. */
    void clear() noexcept {
        Base::clear();

        // shrink to default capacity
        if (!this->frozen() && this->dynamic() && this->capacity != DEFAULT_CAPACITY) {
            this->capacity = DEFAULT_CAPACITY;
            free(table);
            if constexpr (DEBUG) {
                std::cout << "    -> deallocate: " << this->capacity << " nodes";
                std::cout << std::endl;
            }
            table = new Bucket[this->capacity];
            if constexpr (DEBUG) {
                std::cout << "    -> allocate: " << this->capacity << " nodes";
                std::cout << std::endl;
            }
            modulo = this->capacity - 1;
        }
    }

    /* Resize the allocator to store a specific number of nodes. */
    MemGuard reserve(size_t new_size) {
        Base::reserve(new_size);

        // if frozen, check new size against current capacity
        if (this->frozen() || !this->dynamic()) {
            if (new_size > this->capacity - (this->capacity / 4)) {
                throw Base::cannot_grow();
            } else {
                return MemGuard();  // empty guard
            }
        }

        // otherwise, grow the table if necessary
        size_t new_capacity = util::next_power_of_two(new_size + (new_size / 3));
        if (new_capacity > this->capacity) {
            resize(new_capacity);
        }

        // freeze allocator until guard falls out of scope
        return MemGuard(this);
    }

    /* Get the total amount of dynamic memory being managed by this allocator. */
    inline size_t nbytes() const {
        // NOTE: hop information takes 2 extra bytes per bucket (maybe padded to 4/8)
        return sizeof(Node) + this->capacity * sizeof(Bucket);
    }

    /* Get the maximum number of elements that this allocator can support if it does
    not support dynamic sizing. */
    inline std::optional<size_t> max_size() const noexcept {
        return this->dynamic() ? std::nullopt : std::make_optional(max_occupants);
    }

    /* Search for a node by its value directly. */
    template <unsigned int flags = DEFAULT>
    inline Node* search(const Value& key) {
        return _search<flags>(std::hash<Value>{}(key), key);
    }

    /* Search for a node by reusing a hash from another node. */
    template <
        unsigned int flags = DEFAULT,
        typename N,
        bool cond = std::is_base_of_v<NodeTag, N>
    >
    inline auto search(const N* node) -> std::enable_if_t<cond, Node*> {
        if constexpr (NodeTraits<N>::has_hash) {
            return _search<flags>(node->hash(), node->value());
        } else {
            size_t hash = std::hash<typename N::Value>{}(node->value());
            return _search<flags>(hash, node->value());
        }
    }

    // TODO: optimize pop() in the same way.  Search value, get node, incref, unlink,
    // recycle, and return value without any extra lookups.  Perhaps the return value
    // for recycle() can be conditional?  void most of the time, but MappedValue if
    // NodeTraits<Node>::has_mapped && (flags & RETURN_MAPPED)

};


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_CORE_ALLOCATE_H
