#ifndef BERTRAND_STRUCTS_LINKED_BASE_H
#define BERTRAND_STRUCTS_LINKED_BASE_H

#include <cstddef>  // size_t
#include <memory>  // std::shared_ptr, std::weak_ptr
#include <optional>  // std::optional
#include <string_view>  // std::string_view
#include <variant>  // std::visit
#include "core/view.h"  // Views, Direction
#include "../util/iter.h"  // iter(), IterProxy
#include "../util/ops.h"  // PyIterator
#include "../util/python.h"  // python::Slice
#include "../util/string.h"  // string concatenation
#include "../util/thread.h"  // PyLock


namespace bertrand {
namespace linked {


////////////////////
////    BASE    ////
////////////////////


/* Empty tag class marking a linked data structure.  This class is inherited by every
Linked data structure, and can be used for easy SFINAE checks via std::is_base_of
without any foreknowledge of template parameters. */
class LinkedTag {};


/* Base class that forwards the public members of the underlying view. */
template <typename ViewType, typename LockType>
class LinkedBase : public LinkedTag {
    using Direction = linked::Direction;

public:
    using View = ViewType;
    using Allocator = typename View::Allocator;
    using Node = typename View::Node;
    using MemGuard = typename View::MemGuard;
    using Lock = LockType;

    template <Direction dir, Yield yield = Yield::KEY>
    using Iterator = typename View::template Iterator<dir, yield>;
    template <Direction dir, Yield  yield = Yield::KEY>
    using ConstIterator = typename View::template ConstIterator<dir, yield>;

    static constexpr unsigned int FLAGS = View::FLAGS;
    static constexpr bool SINGLY_LINKED = View::SINGLY_LINKED;
    static constexpr bool DOUBLY_LINKED = View::DOUBLY_LINKED;
    static constexpr bool FIXED_SIZE = View::FIXED_SIZE;
    static constexpr bool DYNAMIC = View::DYNAMIC;
    static constexpr bool STRICTLY_TYPED = View::STRICTLY_TYPED;
    static constexpr bool LOOSELY_TYPED = View::LOOSELY_TYPED;

    /* Every linked data structure encapsulates a view that represents the core of the
    data structure and holds the low-level nodes.  For safety, these are not exposed
    directly to the public due to the risk of memory corruption and/or invalidating the
    list.  However, if you know what you're doing, they can be accessed via this
    attribute. */
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
    template <typename Container>
    LinkedBase(
        Container&& iterable,
        std::optional<size_t> max_size = std::nullopt,
        PyObject* spec = nullptr,
        bool reverse = false
    ) : view(std::forward<Container>(iterable), max_size, spec, reverse)
    {}

    /* Construct a list from an iterator range. */
    template <typename Iterator>
    LinkedBase(
        Iterator&& begin,
        Iterator&& end,
        std::optional<size_t> max_size = std::nullopt,
        PyObject* spec = nullptr,
        bool reverse = false
    ) : view(
            std::forward<Iterator>(begin),
            std::forward<Iterator>(end),
            max_size,
            spec,
            reverse
        )
    {}

    /* Construct a list from a base view. */
    LinkedBase(View&& view) : view(std::move(view)) {}

    /* Copy constructor. */
    LinkedBase(const LinkedBase& other) : view(other.view) {}

    /* Move constructor. */
    LinkedBase(LinkedBase&& other) : view(std::move(other.view)) {}

    /* Copy assignment operator. */
    LinkedBase& operator=(const LinkedBase& other) {
        if (this == &other) {
            return *this;
        }
        view = other.view;
        return *this;
    }

    /* Move assignment operator. */
    LinkedBase& operator=(LinkedBase&& other) {
        if (this == &other) {
            return *this;
        }
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

    /* Reserve memory for a specific number of nodes ahead of time.  NOTE: the new
    capacity is absolute, not relative to the current capacity.  If a capacity of 25 is
    requested (for example), then the allocator array will be resized to house at least
    25 nodes, regardless of the current capacity. */
    inline MemGuard reserve(std::optional<size_t> capacity = std::nullopt) {
        return view.reserve(capacity);
    }

    /* Rearrange the allocator array to reflect the current list order. */
    inline void defragment() {
        view.defragment();
    }

    /* Check whether the allocator is currently frozen for memory stability. */
    inline bool frozen() const noexcept {
        return view.frozen();
    }

    /* Get a borrowed reference to the current Python specialization for elements of
    this list. */
    inline PyObject* specialization() const noexcept {
        return view.specialization();
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

    inline Iterator<Direction::FORWARD, Yield::KEY> begin() {
        return view.begin();
    }

    inline Iterator<Direction::FORWARD, Yield::KEY> end() {
        return view.end();
    }

    inline Iterator<Direction::BACKWARD, Yield::KEY> rbegin() {
        return view.rbegin();
    }

    inline Iterator<Direction::BACKWARD, Yield::KEY> rend() {
        return view.rend();
    }

    inline ConstIterator<Direction::FORWARD, Yield::KEY> begin() const {
        return view.begin();
    }

    inline ConstIterator<Direction::FORWARD, Yield::KEY> end() const {
        return view.end();
    }

    inline ConstIterator<Direction::BACKWARD, Yield::KEY> rbegin() const {
        return view.rbegin();
    }

    inline ConstIterator<Direction::BACKWARD, Yield::KEY> rend() const {
        return view.rend();
    }

    inline ConstIterator<Direction::FORWARD, Yield::KEY> cbegin() const {
        return view.cbegin();
    }

    inline ConstIterator<Direction::FORWARD, Yield::KEY> cend() const {
        return view.cend();
    }

    inline ConstIterator<Direction::BACKWARD, Yield::KEY> crbegin() const {
        return view.crbegin();
    }

    inline ConstIterator<Direction::BACKWARD, Yield::KEY> crend() const {
        return view.crend();
    }

    ///////////////////////////////
    ////    THREADING LOCKS    ////
    ///////////////////////////////

    /* Functor that produces threading locks for a linked data structure. */
    Lock lock;
    /* BasicLock:
     * lock()  // lock guard (exclusive)
     *
     * ReadWriteLock:
     * lock()  // lock guard (exclusive)
     * lock.shared()  // lock guard (shared)
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
////    PYTHON WRAPPER    ////
//////////////////////////////


/* A CRTP-enabled base class that exposes properties inherited from LinkedBase to
Python. */
template <typename Derived>
class PyLinkedBase {
protected:

    template <typename Func, typename Result = PyObject*>
    inline static Result visit(Derived* self, Func func, Result err_code = nullptr) {
        try {
            return std::visit(func, self->variant);
        } catch (...) {
            throw_python();
            return err_code;
        }
    }

public:
    PyObject_HEAD

    PyLinkedBase() = delete;
    PyLinkedBase(const PyLinkedBase&) = delete;
    PyLinkedBase(PyLinkedBase&&) = delete;
    PyLinkedBase& operator=(const PyLinkedBase&) = delete;
    PyLinkedBase& operator=(PyLinkedBase&&) = delete;

    inline static PyObject* SINGLY_LINKED(Derived* self, PyObject* = nullptr) noexcept {
        return visit(self, [](auto& list) {
            using List = typename std::decay_t<decltype(list)>;
            return Py_NewRef(List::SINGLY_LINKED ? Py_True : Py_False);
        });
    }

    inline static PyObject* DOUBLY_LINKED(Derived* self, PyObject* = nullptr) noexcept {
        return visit(self, [](auto& list) {
            using List = typename std::decay_t<decltype(list)>;
            return Py_NewRef(List::DOUBLY_LINKED ? Py_True : Py_False);
        });
    }

    inline static PyObject* FIXED_SIZE(Derived* self, PyObject* = nullptr) noexcept {
        return visit(self, [](auto& list) {
            using List = typename std::decay_t<decltype(list)>;
            return Py_NewRef(List::FIXED_SIZE ? Py_True : Py_False);
        });
    }

    inline static PyObject* DYNAMIC(Derived* self, PyObject* = nullptr) noexcept {
        return visit(self, [](auto& list) {
            using List = typename std::decay_t<decltype(list)>;
            return Py_NewRef(List::DYNAMIC ? Py_True : Py_False);
        });
    }

    inline static PyObject* STRICTLY_TYPED(Derived* self, PyObject* = nullptr) noexcept {
        return visit(self, [](auto& list) {
            using List = typename std::decay_t<decltype(list)>;
            return Py_NewRef(List::STRICTLY_TYPED ? Py_True : Py_False);
        });
    }

    inline static PyObject* LOOSELY_TYPED(Derived* self, PyObject* = nullptr) noexcept {
        return visit(self, [](auto& list) {
            using List = typename std::decay_t<decltype(list)>;
            return Py_NewRef(List::LOOSELY_TYPED ? Py_True : Py_False);
        });
    }

    inline static PyObject* lock(Derived* self, PyObject* = nullptr) noexcept {
        return visit(self, [](auto& list) {
            using Lock = typename std::decay_t<decltype(list)>::Lock;
            return bertrand::util::PyLock<Lock>::construct(list.lock);
        });
    }

    inline static PyObject* capacity(Derived* self, PyObject* = nullptr) noexcept {
        return visit(self, [](auto& list) {
            return PyLong_FromSize_t(list.capacity());
        });
    }

    inline static PyObject* max_size(Derived* self, PyObject* = nullptr) noexcept {
        return visit(self, [](auto& list) {
            std::optional<size_t> result = list.max_size();
            if (result.has_value()) {
                return PyLong_FromSize_t(result.value());
            } else {
                Py_RETURN_NONE;
            }
        });
    }

    inline static PyObject* frozen(Derived* self, PyObject* = nullptr) noexcept {
        return visit(self, [](auto& list) {
            return Py_NewRef(list.frozen() ? Py_True : Py_False);
        });
    }

    inline static PyObject* specialization(Derived* self, PyObject* = nullptr) noexcept {
        return visit(self, [](auto& list) {
            PyObject* result = list.specialization();
            if (result == nullptr) {
                Py_RETURN_NONE;
            } else {
                return Py_NewRef(result);
            }
        });
    }

    inline static PyObject* nbytes(Derived* self, PyObject* = nullptr) noexcept {
        return visit(self, [](auto& list) {
            return PyLong_FromSize_t(list.nbytes());
        });
    }

    static PyObject* reserve(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        static constexpr std::string_view meth_name{"reserve"};
        using bertrand::util::parse_opt_int;
        return visit(self, [&args, &nargs](auto& list) {
            using List = typename std::decay_t<decltype(list)>;
            using Allocator = typename List::Allocator;
            using PyMemGuard = typename Allocator::PyMemGuard;

            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            std::optional<long long> capacity = pyargs.parse(
                "capacity", parse_opt_int, std::optional<long long>()
            );
            pyargs.finalize();

            if (capacity.value_or(0) < 0) {
                PyErr_SetString(PyExc_ValueError, "capacity cannot be negative");
                return static_cast<PyObject*>(nullptr);
            }

            size_t size = capacity.value_or(list.size());
            return PyMemGuard::construct(&list.view.allocator, size);
        });
    }

    static PyObject* defragment(Derived* self, PyObject* = nullptr) {
        return visit(self, [](auto& list) {
            list.defragment();
            Py_RETURN_NONE;
        });
    }

    static PyObject* specialize(Derived* self, PyObject* spec) {
        return visit(self, [&spec](auto& list) {
            using List = std::decay_t<decltype(list)>;

            if constexpr (List::STRICTLY_TYPED) {
                PyTypeObject* type = &Derived::Type;
                PyObject* spec = PyObject_GetAttrString(
                    reinterpret_cast<PyObject*>(type),
                    "_specialization"  // injected by __class_getitem__()
                );

                std::ostringstream msg;
                msg << "'" << type->tp_name << "' is already specialized to ";
                if constexpr (NodeTraits<typename List::Node>::has_mapped) {
                    if (PySlice_Check(spec)) {
                        python::Slice<python::Ref::BORROW> slice(spec);
                        msg << "(" << bertrand::repr(slice.start());
                        msg << " : " << bertrand::repr(slice.stop()) << ")";
                    } else {
                        msg << bertrand::repr(spec);
                    }
                } else {
                    msg << bertrand::repr(spec);
                }

                Py_DECREF(spec);
                throw TypeError(msg.str());

            } else {
                list.specialize(bertrand::util::none_to_null(spec));
            }

            Py_RETURN_NONE;
        });
    }

    static PyObject* __class_getitem__(PyObject* type, PyObject* spec) {
        PyObject* heap_type = PyType_FromSpecWithBases(&Specialized::py_spec, type);
        if (heap_type == nullptr) {
            return nullptr;
        }

        if (PyObject_SetAttrString(heap_type, "_specialization", spec) < 0) {
            Py_DECREF(heap_type);
            return nullptr;
        }
        return heap_type;
    }

    inline static Py_ssize_t __len__(Derived* self) noexcept {
        return std::visit(
            [](auto& list) {
                return list.size();
            },
            self->variant
        );
    }

    inline static PyObject* __iter__(Derived* self) noexcept {
        return visit(self, [&self](auto& list) {
            return iter(list).python(reinterpret_cast<PyObject*>(self));
        });
    }

    inline static PyObject* __reversed__(Derived* self, PyObject* = nullptr) noexcept {
        return visit(self, [&self](auto& list) {
            return iter(list).rpython(reinterpret_cast<PyObject*>(self));
        });
    }

    static PyObject* __repr__(Derived* self) {
        return visit(self, [](auto& list) {
            std::ostringstream stream;
            stream << list;
            auto str = stream.str();
            return PyUnicode_FromStringAndSize(str.c_str(), str.size());
        });
    }

protected:

    /* Deallocate the C++ class when its Python reference count falls to zero. */
    inline static void __dealloc__(Derived* self) noexcept {
        PyObject_GC_UnTrack(self);
        self->~Derived();
        Derived::Type.tp_free(reinterpret_cast<PyObject*>(self));
    }

    /* Traversal function for Python's cyclic garbage collector. */
    inline static int __traverse__(Derived* self, visitproc visit, void* arg) noexcept {
        return std::visit(
            [&](auto& list) {
                for (auto item : list) {
                    Py_VISIT(item);
                }
                return 0;
            },
            self->variant
        );
    }

    /* Clear function for Python's cyclic garbage collector. */
    inline static int __clear__(Derived* self) noexcept {
        return std::visit(
            [&](auto& list) {
                // NOTE: normally, we would do something this to support cyclic GC:
                // for (auto item : list) {
                //     Py_CLEAR(item);
                // }
                // However, this is not necessary for linked data structures, since
                // the node destructor effectively accomplishes the same thing.  In
                // fact, if we invoke Py_CLEAR, then we end up double-decrementing the
                // refcount of each item, which can lead to some VERY subtle errors.
                return 0;
            },
            self->variant
        );
    }

    /* Dynamic heap type generated by `__class_getitem__()` in Python. */
    class Specialized {
    public:

        static int __init__(Derived* self, PyObject* args, PyObject* kwargs) {
            using bertrand::util::none_to_null;
            using bertrand::util::parse_int;
            using bertrand::util::is_truthy;
            static constexpr std::string_view meth_name{"__init__"};
            try {
                PyArgs<CallProtocol::KWARGS> pyargs(meth_name, args, kwargs);
                PyObject* iterable = pyargs.parse(
                    "iterable", none_to_null, (PyObject*)nullptr
                );
                std::optional<size_t> max_size = pyargs.parse(
                    "max_size",
                    [](PyObject* obj) -> std::optional<size_t> {
                        if (obj == Py_None) {
                            return std::nullopt;
                        }
                        long long result = parse_int(obj);
                        if (result < 0) {
                            throw ValueError("max_size cannot be negative");
                        }
                        return std::make_optional(static_cast<size_t>(result));
                    },
                    std::optional<size_t>()
                );
                bool reverse = pyargs.parse("reverse", is_truthy, false);
                bool singly_linked = pyargs.parse("singly_linked", is_truthy, false);
                pyargs.finalize();

                PyObject* spec = PyObject_GetAttrString(
                    reinterpret_cast<PyObject*>(Py_TYPE(self)),
                    "_specialization"  // injected by __class_getitem__()
                );
                Derived::initialize(
                    self,
                    iterable,
                    max_size,
                    spec,
                    reverse,
                    singly_linked,
                    true  // strictly typed
                );
                Py_DECREF(spec);

                return 0;

            } catch (...) {
                throw_python();
                return -1;
            }
        }

    private:

        /* Overridden methods for permanently-specialized types. */
        inline static PyMethodDef specialized_methods[] = {
            {"__init__", (PyCFunction)__init__, METH_VARARGS | METH_KEYWORDS, nullptr},
            {NULL}
        };

        /* Overridden slots for permanently-specialized types. */
        inline static PyType_Slot specialized_slots[] = {
            {Py_tp_init, (void*) __init__},
            {Py_tp_methods, specialized_methods},
            {0}
        };

    public:

        /* Overridden type definition for permanently-specialized types. */
        inline static PyType_Spec py_spec = {
            .name = Derived::Type.tp_name,
            .basicsize = 0,  // inherited from base type
            .itemsize = 0,
            .flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
            .slots = specialized_slots
        };

    };

    //////////////////////////
    ////    DOCSTRINGS    ////
    //////////////////////////

    struct docs {

        static constexpr std::string_view SINGLY_LINKED {R"doc(
Check whether the list uses a singly-linked node type.

Returns
-------
bool
    True if the list is singly-linked.  False otherwise.

Notes
-----
This is mutually exclusive with DOUBLY_LINKED.

Examples
--------
This defaults to False unless the list is explicitly configured to use a
singly-linked node type.  For instance:

.. doctest::

    >>> from bertrand.structs import LinkedList
    >>> LinkedList("abcdef").SINGLY_LINKED
    False
    >>> LinkedList("abcdef", singly_linked=True).SINGLY_LINKED
    True
)doc"
        };

        static constexpr std::string_view DOUBLY_LINKED {R"doc(
Check whether the list uses a doubly-linked node type.

Returns
-------
bool
    True if the list is doubly-linked.  False otherwise.

Notes
-----
This is mutually exclusive with SINGLY_LINKED.

Examples
--------
This defaults to True unless the list is explicitly configured to use a
singly-linked node type.  For instance:

.. doctest::

    >>> from bertrand.structs import LinkedList
    >>> LinkedList("abcdef").DOUBLY_LINKED
    True
    >>> LinkedList("abcdef", singly_linked=True).DOUBLY_LINKED
    False
)doc"
        };

        static constexpr std::string_view FIXED_SIZE {R"doc(
Check whether the allocator supports dynamic resizing.

Returns
-------
bool
    True if the list has a fixed maximum size, or False if it can grow and
    shrink dynamically.

Notes
-----
This is mutually exclusive with DYNAMIC.

Examples
--------
This defaults to True unless a maximum size is specified during construction.
For instance:

.. doctest::

    >>> from bertrand.structs import LinkedList
    >>> LinkedList("abcdef").FIXED_SIZE
    False
    >>> LinkedList("abcdef", 10).FIXED_SIZE
    True
)doc"
        };

        static constexpr std::string_view DYNAMIC {R"doc(
Check whether the allocator supports dynamic resizing.

Returns
-------
bool
    True if the list has a fixed maximum size, or False if it can grow and
    shrink dynamically.

Notes
-----
This is mutually exclusive with FIXED_SIZE.

Examples
--------
This defaults to True unless a maximum size is specified during construction.
For instance:

.. doctest::

    >>> from bertrand.structs import LinkedList
    >>> LinkedList("abcdef").DYNAMIC
    True
    >>> LinkedList("abcdef", 10).DYNAMIC
    False
)doc"
        };

        static constexpr std::string_view STRICTLY_TYPED {R"doc(
Check whether the allocator enforces strict type checking.

Returns
-------
bool
    True if the allocator enforces strict type checking for its contents, or
    False if it allows arbitrary Python objects.

Notes
-----
This is mutually exclusive with LOOSELY_TYPED.

Examples
--------
This defaults to False unless the data structure is subscripted with a
particular type using ``__class_getitem__()``.  For instance:

.. doctest::

    >>> from bertrand.structs import LinkedList
    >>> LinkedList("abcdef").STRICTLY_TYPED
    False
    >>> LinkedList[str]("abcdef").STRICTLY_TYPED
    True
)doc"
        };

        static constexpr std::string_view LOOSELY_TYPED {R"doc(
Check whether the allocator enforces strict type checking.

Returns
-------
bool
    True if the allocator enforces strict type checking for its contents, or
    False if it allows arbitrary Python objects.

Notes
-----
This is mutually exclusive with STRICTLY_TYPED.

Examples
--------
This defaults to False unless the data structure is subscripted with a
particular type using ``__class_getitem__()``.  For instance:

.. doctest::

    >>> from bertrand.structs import LinkedList
    >>> LinkedList("abcdef").LOOSELY_TYPED
    True
    >>> LinkedList[str]("abcdef").LOOSELY_TYPED
    False
)doc"
        };

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
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_BASE_H
