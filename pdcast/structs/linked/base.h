// include guard: BERTRAND_STRUCTS_LINKED_BASE_H
#ifndef BERTRAND_STRUCTS_LINKED_BASE_H
#define BERTRAND_STRUCTS_LINKED_BASE_H

#include <cstddef>  // size_t
#include <memory>  // std::shared_ptr, std::weak_ptr
#include <optional>  // std::optional
#include <stdexcept>  // std::runtime_error
#include <string_view>  // std::string_view
#include <variant>  // std::visit
#include "core/iter.h"  // Direction
#include "../util/iter.h"  // iter(), IterProxy
#include "../util/python.h"  // PyIterator
#include "../util/string.h"  // string concatenation
#include "../util/thread.h"  // Lock, PyLock


namespace bertrand {
namespace structs {
namespace linked {


////////////////////
////    BASE    ////
////////////////////


/* Empty tag class marking a linked data structure.

Using an empty class like this allows for easy SFINAE checks via a simple
std::is_base_of check, without requiring any foreknowledge of template parameters. */
class LinkedTag {};


/* Base class that forwards the public members of the underlying view. */
template <typename ViewType, typename LockType>
class LinkedBase : public LinkedTag {
    using Direction = linked::Direction;

public:
    using View = ViewType;
    using Node = typename View::Node;
    using Value = typename View::Value;
    using MemGuard = typename View::MemGuard;
    using Lock = LockType;

    template <Direction dir>
    using Iterator = typename View::template Iterator<dir>;
    template <Direction dir>
    using ConstIterator = typename View::template ConstIterator<dir>;

    /* Every LinkedList contains a view that manages low-level node
    allocation/deallocation and links between nodes. */
    View view;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct an empty list. */
    LinkedBase(
        std::optional<size_t> max_size = std::nullopt,
        PyObject* spec = nullptr
    ) : view(max_size, spec)
    {}

    /* Construct a list from an input iterable. */
    LinkedBase(
        PyObject* iterable,
        std::optional<size_t> max_size = std::nullopt,
        PyObject* spec = nullptr,
        bool reverse = false
    ) : view(iterable, max_size, spec, reverse)
    {}

    /* Construct a list from a base view. */
    LinkedBase(View&& view) : view(std::move(view)) {}

    // TODO: construct from iterators?

    /* Copy constructor. */
    LinkedBase(const LinkedBase& other) : view(other.view) {}

    /* Move constructor. */
    LinkedBase(LinkedBase&& other) : view(std::move(other.view)) {}

    /* Copy assignment operator. */
    LinkedBase& operator=(const LinkedBase& other) {
        view = other.view;
        return *this;
    }

    /* Move assignment operator. */
    LinkedBase& operator=(LinkedBase&& other) {
        view = std::move(other.view);
        return *this;
    }

    //////////////////////////////
    ////    SHARED METHODS    ////
    //////////////////////////////

    /* Check if the list contains any elements. */
    inline bool empty() const noexcept {
        return view.size() == 0;
    }

    /* Get the current size of the list. */
    inline size_t size() const noexcept {
        return view.size();
    }

    /* Get the current capacity of the allocator array. */
    inline size_t capacity() const noexcept {
        return view.capacity();
    }

    /* Get the maximum size of the list. */
    inline std::optional<size_t> max_size() const noexcept {
        return view.max_size();
    }

    /* Check whether the allocator supports dynamic resizing. */
    inline bool dynamic() const noexcept {
        return view.dynamic();
    }

    /* Check whether the allocator is currently frozen for memory stability. */
    inline bool frozen() const noexcept {
        return view.frozen();
    }

    /* Reserve memory for a specific number of nodes ahead of time. */
    inline MemGuard reserve(std::optional<size_t> capacity = std::nullopt) {
        // NOTE: the new capacity is absolute, not relative to the current capacity.  If
        // a capacity of 25 is requested (for example), then the allocator array will be
        // resized to house at least 25 nodes, regardless of the current capacity.
        return view.reserve(capacity);
    }

    /* Rearrange the allocator array to reflect the current list order. */
    inline void defragment() {
        view.defragment();
    }

    /* Get the current specialization for elements of this list. */
    inline PyObject* specialization() const noexcept {
        return view.specialization();  // TODO: reference counting?
    }

    /* Enforce strict type checking for elements of the list. */
    inline void specialize(PyObject* spec) {
        view.specialize(spec);
    }

    /* Get the total amount of memory consumed by the list. */
    inline size_t nbytes() const noexcept {
        return sizeof(LinkedBase) + view.nbytes();
    }

    /////////////////////////////////
    ////    ITERATOR PROTOCOL    ////
    /////////////////////////////////

    inline Iterator<Direction::forward> begin() { return view.begin(); }
    inline Iterator<Direction::forward> end() { return view.end(); }
    inline Iterator<Direction::backward> rbegin() { return view.rbegin(); }
    inline Iterator<Direction::backward> rend() { return view.rend(); }
    inline ConstIterator<Direction::forward> begin() const { return view.begin(); }
    inline ConstIterator<Direction::forward> end() const { return view.end(); }
    inline ConstIterator<Direction::backward> rbegin() const { return view.rbegin(); }
    inline ConstIterator<Direction::backward> rend() const { return view.rend(); }
    inline ConstIterator<Direction::forward> cbegin() const { return view.cbegin(); }
    inline ConstIterator<Direction::forward> cend() const { return view.cend(); }
    inline ConstIterator<Direction::backward> crbegin() const { return view.crbegin(); }
    inline ConstIterator<Direction::backward> crend() const { return view.crend(); }

    ///////////////////////////////
    ////    THREADING LOCKS    ////
    ///////////////////////////////

    /* Functor that produces threading locks for a linked data structure. */
    Lock lock;
    /* BasicLock:
     * lock()  // lock guard
     * lock.python()  // context manager
     *
     * ReadWriteLock:
     * lock()  // lock guard (exclusive)
     * lock.python()  // context manager (exclusive)
     * lock.shared()  // lock guard (shared)
     * lock.shared.python()  // context manager (shared)
     *
     * RecursiveLock<Lock>:
     * Allows the above methods to be called recursively within a single thread.
     *
     * SpinLock<Lock>:
     * Adds a spin effect to lock acquisition, with optional timeout and sleep interval
     * between each cycle.
     *
     * DiagnosticLock<Lock>:
     * Tracks diagnostics about lock acquisition and release.
     * lock.count()  // number of times the lock has been acquired
     * lock.duration()  // total time spent acquiring the lock
     * lock.contention()  // average time spent acquiring the lock
     * lock.reset_diagnostics()  // reset the above values to zero
     */

};


//////////////////////////////
////    CYTHON HELPERS    ////
//////////////////////////////


namespace cython {


/* A functor that generates weak references for a type-erased Cython variant. */
template <typename T>
class SelfRef {
public:

    /* A weak reference to the associated object. */
    class WeakRef {
    public:

        /* Check whether the referenced object still exists. */
        bool exists() const {
            return !ref.expired();
        }

        /* Follow the weak reference, yielding a pointer to the referenced object if it
        still exists.  Otherwise, sets a Python error and return nullptr.  */
        T* get() const {
            if (ref.expired()) {
                throw std::runtime_error("referenced object no longer exists");
            }
            return ref.lock().get();
        }

    private:
        friend SelfRef;
        std::weak_ptr<T> ref;

        template <typename... Args>
        WeakRef(Args... args) : ref(std::forward<Args>(args)...) {}
    };

    /* Get a weak reference to the associated object. */
    WeakRef operator()() const {
        return WeakRef(_self);
    }

private:
    friend T;
    const std::shared_ptr<T> _self;

    // NOTE: custom deleter prevents the shared_ptr from trying to delete the object
    // when it goes out of scope, which can cause a segfault due to a double free.

    SelfRef(T& self) : _self(&self, [](auto&) {}) {}
};


}  // namespace cython


//////////////////////////////
////    PYTHON WRAPPER    ////
//////////////////////////////


// TODO: SelfRef is only needed for RelativeProxies, which are not yet implemented.


/* A CRTP-enabled base class that exposes properties inherited from LinkedBase to
Python. */
template <typename Derived>
class PyLinkedBase {
    PyObject_HEAD

public:

    /* C++ constructors/assignment operators deleted for compatibility with Python
    API.

    NOTE: the Python C API does not always respect C++ construction semantics, and can
    lead to some very subtle bugs related to memory initialization, particularly as it
    relates to Python object headers and stack-allocated memory.  We're better off
    disabling C++ constructors entirely in favor of explicit factory methods instead. */
    PyLinkedBase() = delete;
    PyLinkedBase(const PyLinkedBase&) = delete;
    PyLinkedBase(PyLinkedBase&&) = delete;
    PyLinkedBase& operator=(const PyLinkedBase&) = delete;
    PyLinkedBase& operator=(PyLinkedBase&&) = delete;

    /* Wrap LinkedList.lock functor as a managed @property in Python. */
    inline static PyObject* lock(Derived* self, void* /* ignored */) noexcept {
        return std::visit(
            [](auto& list) {
                using Lock = typename std::decay_t<decltype(list)>::Lock;
                return util::PyLock<Lock>::create(list.lock);
            },
            self->variant
        );
    }

    /* Getter for `LinkedList.capacity` in Python. */
    inline static PyObject* capacity(Derived* self, PyObject* /* ignored */) noexcept {
        size_t result = std::visit(
            [](auto& list) {
                return list.capacity();
            },
            self->variant
        );
        return PyLong_FromSize_t(result);
    }

    /* Getter for `LinkedList.max_size` in Python. */
    inline static PyObject* max_size(Derived* self, PyObject* /* ignored */) noexcept {
        std::optional<size_t> result = std::visit(
            [](auto& list) {
                return list.max_size();
            },
            self->variant
        );
        if (result.has_value()) {
            return PyLong_FromSize_t(result.value());
        } else {
            Py_RETURN_NONE;
        }
    }

    /* Getter for `LinkedList.dynamic` in Python. */
    inline static PyObject* dynamic(Derived* self, PyObject* /* ignored */) noexcept {
        bool result = std::visit(
            [](auto& list) {
                return list.dynamic();
            },
            self->variant
        );
        return PyBool_FromLong(result);
    }

    /* Getter for `LinkedList.frozen` in Python. */
    inline static PyObject* frozen(Derived* self, PyObject* /* ignored */) noexcept {
        bool result = std::visit(
            [](auto& list) {
                return list.frozen();
            },
            self->variant
        );
        return PyBool_FromLong(result);
    }

    /* Getter for `LinkedList.nbytes` in Python. */
    inline static PyObject* nbytes(Derived* self, PyObject* /* ignored */) noexcept {
        size_t result = std::visit(
            [](auto& list) {
                return list.nbytes();
            },
            self->variant
        );
        return PyLong_FromSize_t(result);
    }

    /* Getter for `LinkedList.specialization` in Python. */
    inline static PyObject* specialization(Derived* self, PyObject* /* ignored */) noexcept {
        PyObject* result = std::visit(
            [](auto& list) {
                return list.specialization();
            },
            self->variant
        );
        if (result == nullptr) {
            Py_RETURN_NONE;
        }
        return Py_NewRef(result);
    }

    // TODO: For reserve() to be available at the Python level, we need to create a
    // Python wrapper like we did with threading locks/iterators.

    /* Implement `LinkedList.reserve()` in Python. */
    static PyObject* reserve(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        using Args = util::PyArgs<util::CallProtocol::FASTCALL>;
        using Index = std::optional<long long>;
        try {
            // parse arguments
            Args pyargs = Args(args, nargs);
            Index capacity = pyargs.parse("capacity", util::parse_opt_int, Index());
            pyargs.finalize();

            // TODO: Convert the C++ MemGuard into a Python context manager
            return nullptr;

        // translate C++ exceptions into Python errors
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedList.defragment()` in Python. */
    static PyObject* defragment(Derived* self, PyObject* /* ignored */) {
        try {
            std::visit(
                [](auto& list) {
                    list.defragment();
                },
                self->variant
            );
            Py_RETURN_NONE;  // void

        // translate C++ exceptions into Python errors
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
    }

    /* Implement `LinkedList.specialize()` in Python. */
    static PyObject* specialize(Derived* self, PyObject* spec) {
        try {
            std::visit(
                [&spec](auto& list) {
                    list.specialize(spec);
                },
                self->variant
            );
            Py_RETURN_NONE;  // void

        // translate C++ exceptions into Python errors
        } catch (...) {
            util::throw_python();
            return nullptr;
        }
        
    }

    /* Implement `LinkedList.__class_getitem__()` in Python. */
    static PyObject* __class_getitem__(PyObject* type, PyObject* spec) {
        // create a new heap type for the specialization
        PyObject* specialized_type = PyType_FromSpecWithBases(
            &Specialized::specialized_spec,
            type
        );
        if (specialized_type == nullptr) return nullptr;

        // set the specialization attribute
        if (PyObject_SetAttrString(specialized_type, "_specialization", spec) < 0) {
            Py_DECREF(specialized_type);
            return nullptr;
        }

        // return the new type
        return specialized_type;
    }

    /* Implement `LinkedList.__len__()` in Python. */
    inline static Py_ssize_t __len__(Derived* self) noexcept {
        return std::visit(
            [](auto& list) {
                return list.size();
            },
            self->variant
        );
    }

    /* Implement `LinkedList.__iter__()` in Python. */
    inline static PyObject* __iter__(Derived* self) noexcept {
        return std::visit(
            [](auto& list) {
                return util::iter(list).python();
            },
            self->variant
        );
    }

    /* Implement `LinkedList.__reversed__()` in Python. */
    inline static PyObject* __reversed__(Derived* self, PyObject* /* ignored */) noexcept {
        return std::visit(
            [](auto& list) {
                return util::iter(list).rpython();
            },
            self->variant
        );
    }

protected:

    /* Allocate a new LinkedList instance from Python and register it with the cyclic
    garbage collector. */
    inline static PyObject* __new__(
        PyTypeObject* type,
        PyObject* /* ignored */,
        PyObject* /* ignored */
    ) {
        Derived* self = reinterpret_cast<Derived*>(type->tp_alloc(type, 0));
        if (self == nullptr) return nullptr;
        return reinterpret_cast<PyObject*>(self);
    }

    /* Deallocate the LinkedList when its Python reference count falls to zero. */
    inline static void __dealloc__(Derived* self) noexcept {
        PyObject_GC_UnTrack(self);  // unregister from cyclic garbage collector
        self->~Derived();  // hook into C++ destructor
        Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
    }

    /* Traversal function for Python's cyclic garbage collector. */
    inline static int __traverse__(Derived* self, visitproc visit, void* arg) noexcept {
        return std::visit(
            [&](auto& list) {
                for (auto item : list) Py_VISIT(item);
                return 0;
            },
            self->variant
        );
    }

    /* Clear function for Python's cyclic garbage collector. */
    inline static int __clear__(Derived* self) noexcept {
        return std::visit(
            [&](auto& list) {
                for (auto item : list) Py_CLEAR(item);
                return 0;
            },
            self->variant
        );
    }

    /* Dynamic heap type generated by `LinkedList.__class_getitem__()` in Python. */
    class Specialized : public Derived {
    public:

        /* Initialize a permanently-specialized LinkedList instance from
        __class_getitem__(). */
        static int __init__(
            Specialized* self,
            PyObject* args,
            PyObject* kwargs
        ) {
            using Args = util::PyArgs<util::CallProtocol::KWARGS>;
            using util::ValueError;
            try {
                // parse arguments
                Args pyargs(args, kwargs);
                PyObject* iterable = pyargs.parse(
                    "iterable", util::none_to_null, (PyObject*)nullptr
                );
                std::optional<size_t> max_size = pyargs.parse(
                    "max_size",
                    [](PyObject* obj) -> std::optional<size_t> {
                        if (obj == Py_None) return std::nullopt;
                        long long result = util::parse_int(obj);
                        if (result < 0) throw ValueError("max_size cannot be negative");
                        return std::make_optional(static_cast<size_t>(result));
                    },
                    std::optional<size_t>()
                );
                bool reverse = pyargs.parse("reverse", util::is_truthy, false);
                bool singly_linked = pyargs.parse("singly_linked", util::is_truthy, false);
                pyargs.finalize();

                // initialize
                using Variant = typename Specialized::Variant;
                using SingleList = typename Specialized::SingleList;
                using DoubleList = typename Specialized::DoubleList;
                PyObject* spec = PyObject_GetAttrString(
                    reinterpret_cast<PyObject*>(Py_TYPE(self)),
                    "_specialization"
                );
                if (spec == Py_None) spec = nullptr;
                if (iterable == nullptr) {
                    if (singly_linked) {
                        new (&self->variant) Variant(SingleList(max_size, spec));
                    } else {
                        new (&self->variant) Variant(DoubleList(max_size, spec));
                    }
                } else {
                    if (singly_linked) {
                        new (&self->variant) Variant(
                            SingleList(iterable, max_size, spec, reverse)
                        );
                    } else {
                        new (&self->variant) Variant(
                            DoubleList(iterable, max_size, spec, reverse)
                        );
                    }
                }

                // exit normally
                return 0;

            // translate C++ errors into Python exceptions
            } catch (...) {
                util::throw_python();
                return -1;
            }
        }

        /* Disable dynamic specialization for permanently-specialized types. */
        static PyObject* specialize(Specialized* self, PyObject* /* ignored */) {
            PyTypeObject* type = Py_TYPE(self);
            PyObject* spec = PyObject_GetAttrString(
                reinterpret_cast<PyObject*>(type),
                "_specialization"
            );
            PyErr_Format(
                PyExc_TypeError,
                "'%s' is already specialized to %R",
                type->tp_name,
                spec
            );
            return nullptr;
        }

    private:

        /* Overridden methods for permanently-specialized types. */
        inline static PyMethodDef specialized_methods[] = {
            {"__init__", (PyCFunction)__init__, METH_VARARGS | METH_KEYWORDS, nullptr},
            {"specialize", (PyCFunction)specialize, METH_O, nullptr},
            {NULL}  // sentinel
        };

        /* Overridden slots for permanently-specialized types. */
        inline static PyType_Slot specialized_slots[] = {
            {Py_tp_init, (void*) __init__},
            {Py_tp_methods, specialized_methods},
            {0}  // sentinel
        };

    public:

        /* Overridden type definition for permanently-specialized types. */
        inline static PyType_Spec specialized_spec = {
            .name = Derived::Type.tp_name,
            .basicsize = sizeof(Specialized),
            .itemsize = 0,
            .flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
            .slots = specialized_slots
        };

    };

    //////////////////////////
    ////    DOCSTRINGS    ////
    //////////////////////////

    /* Compile-time docstrings for all public Python methods. */
    struct docs {

        static constexpr std::string_view lock {R"doc(
Access the list's internal thread lock.

Returns
-------
PyLock
    A proxy for a C++ lock functor that can be used to acquire threading locks
    on the list.

Notes
-----
By default, none of the list's methods are guaranteed to be thread-safe.  This
guarantees the highest performance in a single-threaded context, but means that
concurrent access from multiple threads can lead to undefined behavior.  To
mitigate this and allow for parallelism, the list offers a built-in mutex that
can be used to synchronize access between threads.

Each list's lock can configured using constructor arguments, like so:

.. doctest::

    >>> from bertrand.structs import LinkedList
    >>> from datetime import timedelta
    >>> lock_config = {
    ...     "shared": false,
    ...     "recursive": false,
    ...     "retries": -1,
    ...     "timeout": timedelta(microseconds=-1),
    ...     "wait": timedelta(microseconds=-1),
    ...     "diagnostic": false
    ... }
    >>> l = LinkedList("abcdef", lock=lock_config)

The meaning of each argument is as follows:

    #. ``shared``: enables the ``lock.shared()`` method, allowing the lock to
        be acquired by multiple readers at once.  This allows for concurrent
        reads of the list as long as no writers are waiting to acquire the
        lock in exclusive mode.
    #. ``recursive``: Allows the lock to be acquired in nested contexts within
        the same thread.  Only the outermost guard will actually acquire the
        mutex.  Inner locks are no-ops.  Generally speaking, algorithms should
        not rely on this behavior, and should be refactored to avoid recursive
        locking if possible.  However, it can be useful for debugging purposes
        when trying to track down the source of a deadlock, or when refactoring
        the problematic code is not feasible.
    #. ``retries``: The number of times to retry the lock before giving up.
        This introduces a spin effect that can be useful for reducing the
        latency of lock acquisition in low-contention scenarios.  If this is
        set to a negative number (the default), then the lock will simply block
        until it can be acquired.  If it is set to zero, then it will
        immediately raise an error if the lock is unavailable.  Otherwise, it
        will busy-wait until the lock is available, up to the specified number
        of retries.
    #. ``timeout``: The maximum amount of time to wait for the lock to become
        available.  If this is set to a negative number (the default), then the
        lock will wait indefinitely to acquire the lock.  If it is set to zero,
        then it will immediately raise an error if the lock is unavailable.
        Otherwise, it will allow the lock to wait up to the specified length of
        time, and will raise an error if it times out.
    #. ``wait``: The amount of time to wait between each acquisition attempt.
        This is only used if ``retries`` is set to a positive number.  If this
        is set to a negative number or zero (the default), then the lock will
        immediately retry without waiting, causing a busy-wait cycle.
        Otherwise, it will wait for the specified length of time and then retry
        the lock.
    #.  ``diagnostic``: Enables diagnostic tracking for the lock.  This will
        track the number of times the lock has been acquired, as well as the
        total amount of time spent waiting to acquire the lock.  This can be
        used to track the average contention for the lock, which can be useful
        when profiling multithreaded code.

These arguments can be stacked together to create a wide variety of lock
behaviors.  Consider the following configuration:

.. code_block:: python

    lock_config = {
        "shared": True,
        "recursive": True,
        "timeout": timedelta(seconds=1),
        "wait": timedelta(milliseconds=100),
        "diagnostic": True
    }

This will create a shared lock that can be acquired by multiple readers, each
of which can reacquire it recursively within the same thread.  It will wait up
to one second to acquire the lock, and will retry every 100 milliseconds until
it becomes available.  It will also track diagnostic information about the lock
acquisition process as it is used.

Examples
--------
The lock itself is a functor (function object) that is exposed as a read-only
@property in Python.

.. doctest::

    >>> from bertrand.structs import LinkedList
    >>> l = LinkedList("abcdef")
    >>> l.lock
    <PyLock object at 0x7f9c3c0b3a00>

On access, it produces a Python proxy for the list's underlying C++ lock, which
can be called like a normal method to acquire the lock in exclusive mode.

.. doctest::

    >>> l.lock()
    <PyGuard object at 0x7f9c3c0b3a00>

The returned guard can then be used as a context manager to lock the mutex.

.. doctest::

    >>> with l.lock():
    ...     # do something with the list
    ...     pass

The mutex will be automatically unlocked when the context manager exits.

Besides the lock itself, the functor also exposes a number of additional
methods/properties based on the configuration options described above.  These
are available from the proxy as member attributes.

.. note::

    The behavior of the context manager closely mimics that of a
    ``std::unique_lock`` at the C++ level.  In fact, it is directly
    equivalent in the back-end implementation.  When the context manager is
    entered, it constructs a ``std::unique_lock`` on the underlying mutex, and
    deletes it on exit.

)doc"
        };

        static constexpr std::string_view capacity {R"doc(
Get the current capacity of the allocator array.

Returns
-------
int
    The total number of allocated nodes.

Notes
-----
This will always be greater than or equal to the current size of the list, in
accordance with the allocator's growth strategy.  If ``dynamic`` is not set,
then it will always be equal to the maximum size of the list.
)doc"
        };

        static constexpr std::string_view max_size {R"doc(
Get the maximum size of the list.

Returns
-------
int or None
    The maximum size of the list, or None if the list is unbounded.

Notes
-----
This is equivalent to the following:

.. code-block:: python

    if list.dynamic:
        return None
    else:
        return list.capacity
)doc"
        };

        static constexpr std::string_view dynamic {R"doc(
Check whether the allocator supports dynamic resizing.

Returns
-------
bool
    True if the list can dynamically grow and shrink, or False if it has a
    fixed size.

Examples
--------
This defaults to True unless a maximum size is specified during construction.
For instance:

.. doctest::

    >>> from bertrand.structs import LinkedList
    >>> LinkedList("abcdef").dynamic
    True
    >>> LinkedList("abcdef", 10).dynamic
    False
)doc"
        };

        static constexpr std::string_view frozen {R"doc(
Check whether the allocator is temporarily frozen for memory stability.

Returns
-------
bool
    True if the memory addresses of the list's nodes are guaranteed to remain
    stable within the current context, or False if they may be reallocated.

Notes
-----
Dynamically-growing data structures may need to reallocate their internal memory
array when they reach a certain load factor.  When this occurs, the nodes are
physically transferred to a new memory location and the old addresses are freed.
If any external pointers/references to the old locations exist, then they will
become dangling through this process, and can lead to undefined behavior if
dereferenced.  This is particularly a problem for algorithms that iterate over
a list while modifying it, as arbitrarily adding or removing nodes can trigger
a resize that will invalidate the iterator.

Thankfully, this can be avoided by temporarily freezing the allocator, which
can be done through the ``reserve()`` method, like so:

.. code-block:: python

    with list.reserve():
        # nodes are guaranteed to remain stable within this context
        assert list.frozen  # True
    # nodes may be reallocated after this point
    assert not list.frozen  # False

This guarantees that the list's memory addresses will not change within the
frozen context, consequently making iteration safe to perform.

Users should not usually need to worry about this, as all of the built-in
algorithms are designed with this in mind.  However, it is important to be
aware of this behavior when writing custom extensions, as it can lead to subtle
bugs if not handled properly.
)doc"
        };

        static constexpr std::string_view nbytes {R"doc(
The total memory consumption of the list (in bytes).

Returns
-------
int
    The total number of bytes consumed by the list, including all its control
    structures and nodes.

Notes
-----
This does not account for any additional dynamic memory allocated by the values
themselves (e.g. via heap allocations, resizable buffers, etc.).
)doc"
        };

        static constexpr std::string_view specialization {R"doc(
Get the type specialization currently enforced by the list.

Returns
-------
type
    The current specialization for elements of this list, or ``None`` if the
    list is generic.

Notes
-----
This is equivalent to the ``spec`` argument passed to the constructor and/or
``specialize()`` method.

When a specialization is set, the list will enforce strict type checking for
any node that is added to it.  If a node does not contain a value matching the
specialization as supplied to ``isinstance()``, then it will be rejected and
an error will be raised.  This is useful for ensuring that all elements of a
list are of a consistent type, and can be used to prevent accidental type
errors from propagating through the list.  It also preserves as much
performance as possible by applying type checks at node insertion time, leaving
the rest of the list's algorithms at their native speed.
)doc"
        };

        static constexpr std::string_view defragment {R"doc(
Reallocate the allocator array in-place to consolidate memory and improve cache
performance.

Notes
-----
Occasionally, as elements are added and removed from the list, the allocator
array can become fragmented, with nodes scattered throughout memory.  This can
degrade performance by increasing the number of cache misses and probing steps
that must be performed to access each node.  This method optimizes the list by
rearranging the existing nodes into a more contiguous layout, so that they can
be accessed more efficiently.

This method is relatively expensive, as it incurs a full loop through the list.
For performance-critical applications, it is recommended to call it only when
the list is expected to be in a fragmented state, or during a period of low
activity where the cost can be amortized.
)doc"
        };

        static constexpr std::string_view reserve {R"doc(
Allocate enough memory to store the specified number of nodes and then freeze
the allocator at the new size.

Parameters
----------
int or None
    The new size of the list after the allocator is resized.  If this is set to
    ``None``, then the allocator will be frozen at its current size.

Raises
------
MemoryError
    If the system runs out of memory, or if the resize would cause a frozen
    list to exceed its current capacity.

Returns
-------
MemGuard
    A context manager that freezes and unfreezes the allocator when entering
    and exiting the context.  Any operations within the guarded context are
    guaranteed to be memory-stable.

Notes
-----
This method uses a non-pessimized growth strategy to allocate the required
memory, which is more efficient than the more direct approach used by other C++
data structures like ``std::vector``.  It is thus safe to call this method
repeatedly without incurring a large performance penalty.

The way this works is by rounding the requested size up to the nearest power of
two and then growing the allocator if and only if that size is greater than the
current capacity.  This ensures that the allocator will never be resized more
than once, and preserves the allocator's ordinary geometric growth strategy for
maximum efficiency.

After resizing the allocator, this method produces a memory guard that freezes
it at the new size.  The guard can be used as a context manager that freezes
the allocator at the new size when entering the context and unfreezes it on
exit.  This allows the user to guarantee memory stability for any operations
performed within the context, which is useful for low-level algorithms that
need to avoid dynamic resizing and reallocation.

Upon exiting the guarded context, the allocator will be unfrozen, and will be
allowed to grow or shrink as needed.  If the current occupancy is below the
minimum load factor (12.5-25%, typically), then the allocator will also be
shrunk to fit.  This prevents unnecessary memory bloat and includes hysteresis
to prevent thrashing.

Examples
--------
.. doctest::

    >>> from bertrand.structs import LinkedList
    >>> l = LinkedList("abcdef")
    >>> l.capacity
    8
    >>> with l.reserve(100):
    ...     print(l.capacity)
    128
    >>> l.capacity
    16

)doc"
        };

        static constexpr std::string_view specialize {R"doc(
Enforce strict type checking for elements of the list.

Parameters
----------
spec : type
    The type to enforce for all elements of the list.  This can be in any
    format recognized by :func:`isinstance() <python::isinstance>`.  If it is
    set to ``None``, then type checking will be disabled for the list.

Raises
------
TypeError
    If the list contains elements that do not match the specified type.

Notes
-----
If the list is not empty when this method is called, then the type of each
existing item will be checked against the new type.  If any of them do not
match, then the specialization will be aborted and an error will be raised.
The list is not modified during this process.

Any elements added to the list after this method is called must conform to the
specified type, otherwise an error will be raised.  The list is thus considered
to be type-safe as long as the specialization is active.
)doc"
        };

        static constexpr std::string_view __class_getitem__ {R"doc(
Subscript the container to create a permanently-specialized type.

Parameters
----------
spec : Any
    The type to enforce for elements of the list.  This can be in any format
    recognized by :func:`isinstance() <python:isinstance>`, including tuples and
    :func:`runtime-checkable <python:typing.runtime_checkable>`
    :class:`typing.Protocol <python:typing.Protocol>` objects.

Returns
-------
type
    A new heap type that enforces the specified type specialization for all
    elements of the list.  These types cannot be re-specialized after creation.

Notes
-----
Constructing a permanently-specialized type is equivalent to calling the
constructor with the optional ``spec`` argument, except that the specialization
cannot be changed for the lifetime of the object.  This allows the user to be
absolutely sure that the list will always contain elements of the specified
type, and can be useful for creating custom data structures that are guaranteed
to be type-safe at the Python level.

Examples
--------
.. doctest::

    >>> l = LinkedList[int]([1, 2, 3])
    >>> l.specialization
    <class 'int'>
    >>> l
    LinkedList[<class 'int'>]([1, 2, 3])
    >>> l.append(4)
    >>> l
    LinkedList[<class 'int'>]([1, 2, 3, 4])
    >>> l.append("a")
    Traceback (most recent call last):
        ...
    TypeError: 'a' is not of type <class 'int'>
    >>> l.specialize(str)
    Traceback (most recent call last):
        ...
    TypeError: LinkedList is already specialized to <class 'int'>

Because type specialization is enforced through the built-in
:func:`isinstance() <python:isinstance>` function, it is possible to subscript
a list with any type that implements the
:meth:`__instancecheck__() <python:object.__instancecheck__>` special method,
including :func:`runtime-checkable <python:typing.runtime_checkable>`
:class:`typing.Protocol <python:typing.Protocol>` objects.

.. doctest::

    >>> from typing import Iterable

    >> l = LinkedList[Iterable]()
    >>> l.append([1, 2, 3])
    >>> l
    LinkedList[typing.Iterable]([[1, 2, 3]])
    >>> l.append("abc")
    >>> l
    LinkedList[typing.Iterable]([[1, 2, 3], 'abc'])
    >>> l.append(4)
    Traceback (most recent call last):
        ...
    TypeError: 4 is not of type typing.Iterable

)doc"
        };

        static constexpr std::string_view __reversed__ {R"doc(
Get a reverse iterator over the list.

Returns
-------
iter
    A reverse iterator over the list.

Notes
-----
This method is used by the built-in :func:`reversed() <python:reversed>`
function to iterate over the list in reverse order.

Note that reverse iteration has different performance characteristics for
doubly-linked lists vs singly-linked ones.  For the former, we can iterate in
either direction with equal efficiency, but for the latter, we must construct
a temporary stack to store the nodes in reverse order.  This can be expensive
in both time and memory, requiring two full iterations over the list rather
than one.
)doc"
        };

    };

};


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_BASE_H
