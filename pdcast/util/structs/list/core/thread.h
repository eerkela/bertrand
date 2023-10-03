// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_THREAD_H
#define BERTRAND_STRUCTS_CORE_THREAD_H

#include <chrono>  // std::chrono
#include <memory>  // std::shared_ptr, std::weak_ptr
#include <mutex>  // std::mutex, std::lock_guard, std::unique_lock
#include <optional>  // std::optional
#include <shared_mutex>  // std::shared_mutex, std::shared_lock
#include <thread>  // std::thread
#include <unordered_set>  // std::unordered_set

#include "util.h"  // Slot


// TODO: Locks are implemented in LinkedBase, just like iterators.  LinkedBase must
// accept the lock type as a template parameter, and that template parameter must
// implement a lock.python() method that returns a Python context manager.



////////////////////////
////    FUNCTORS    ////
////////////////////////


/* Locks are functors (function objects) that produce std::lock_guards for an internal
 * mutex.  They can be applied to any data structure, and are generally safer than
 * manually locking and unlocking the mutex.
 * 
 * DiagnosticLock is a variation of BasicLock that provides the same functionality, but
 * also keeps track of basic diagnostics, including the total number of times the mutex
 * has been locked and the average contention time for each lock.  This is useful for
 * profiling the performance of threaded code and identifying potential bottlenecks.
 */


/* A lock functor that uses a simple mutex and a `std::unique_lock` guard. */
class BasicLock {
public:
    using Mutex = std::mutex;
    using Guard = std::unique_lock<std::mutex>;
    inline static constexpr bool is_shared = false;

    /* Return a std::unique_lock for the internal mutex using RAII semantics.  The
    mutex is automatically acquired when the guard is constructed and released when it
    goes out of scope.  Any operations in between are guaranteed to be atomic. */
    inline Guard operator()() const noexcept { return Guard(mtx); }

protected:
    mutable Mutex mtx;
};


/* A lock functor that uses a shared mutex for concurrent reads and exclusive writes. */
class ReadWriteLock {
public:
    using Mutex = std::shared_mutex;
    using Guard = std::unique_lock<std::shared_mutex>;
    using SharedGuard = std::shared_lock<std::shared_mutex>;
    inline static constexpr bool is_shared = true;

    /* Return a std::unique_lock for the internal mutex using RAII semantics.  The
    mutex is automatically acquired when the guard is constructed and released when it
    goes out of scope.  Any operations in between are guaranteed to be atomic.*/
    inline Guard operator()() const noexcept { return Guard(mtx); }

    /* Return a std::shared_lock for the internal mutex.

    NOTE: These locks allow concurrent access to the object as long as no exclusive
    guards have been requested.  This is useful for read-only operations that do not
    modify the underlying data structure.  They have the save RAII semantics as the
    normal call operator. */
    inline SharedGuard shared() const { return SharedGuard(mtx); }

protected:
    mutable Mutex mtx;
};


//////////////////////////
////    DECORATORS    ////
//////////////////////////


/* Base class specialization for non-shared lock functors. */
template <typename Lock, bool is_shared = false>
class _RecursiveLock : public Lock {};


/* Base class specialization for shared lock functors. */
template <typename Lock>
class _RecursiveLock<Lock, true> : public Lock {
    friend struct SharedGuard;
    using SharedWrapped = typename Lock::SharedGuard;
    mutable std::unordered_set<std::thread::id> shared_owners;

public:

    /* A proxy for the wrapped lock guard that allows recursive references using the
    functor's reference counter. */
    struct SharedGuard {
        _RecursiveLock& lock;
        std::optional<SharedWrapped> guard;

        /* Construct an empty guard proxy. */
        SharedGuard(_RecursiveLock& lock) : lock(lock) {}

        /* Construct the outermost guard proxy for the recursive lock. */
        SharedGuard(_RecursiveLock& lock, SharedWrapped guard) : lock(lock), guard(guard) {}

        /* Disabled copy constructor/assignment for compatibility with
        std::unique_lock. */
        SharedGuard(const SharedGuard&) = delete;
        SharedGuard& operator=(const SharedGuard&) = delete;

        /* Move constructor. */
        SharedGuard(SharedGuard&& other) : lock(other.lock), guard(std::move(other.guard)) {}

        /* Move assignment operator. */
        SharedGuard& operator=(SharedGuard&& other) {
            lock = other.lock;
            guard = std::move(other.guard);
            return *this;
        }

        /* Destroy the guard proxy and remove the shared lock from the pool. */
        ~SharedGuard() {
            if (guard.has_value()) {
                lock.shared_owners.erase(std::this_thread::get_id());
            }
        }

    };

    /* Acquire the lock in shared mode, allowing repeated locks within a single
    thread. */
    template <typename... Args>
    inline SharedGuard shared(Args&&... args) const {
        auto id = std::this_thread::get_id();

        // if the current thread already owns the lock, return an empty guard
        if (shared_owners.count(id)) {
            return SharedGuard(*this);
        }

        // NOTE: the handling of the `shared_owners` set is only thread-safe due to the
        // exact order of operations here.  As written, the set is only modified AFTER
        // a lock has been acquired, which guarantees that we will not encounter any
        // race conditions during our comparisons.

        // If two threads attempt to acquire the lock at the same time, the owner set
        // will only be modified after the first thread has acquired the lock.  If this
        // blocks for some reason (e.g. due to the presence of an exclusive lock on the
        // same mutex), then the owner set will not be modified until the lock is
        // successfully acquired.  The second thread will then proceed to acquire the
        // lock and update the owner set accordingly.

        // otherwise, attempt to acquire the lock
        SharedWrapped guard = Lock::shared(std::forward<Args>(args)...);  // blocks
        shared_owners.insert(id);
        return SharedGuard(*this, std::move(guard));
    }

};


/* A lock decorator that allows a thread to be locked recursively without
deadlocking. */
template <typename Lock>
class RecursiveLock : public _RecursiveLock<Lock, Lock::is_shared> {
    friend struct Guard;
    using Wrapped = typename Lock::Guard;
    mutable std::thread::id owner;

public:

    /* A proxy for the wrapped lock guard that allows recursive references using the
    functor's reference counter. */
    struct Guard {
        RecursiveLock& lock;
        std::optional<Wrapped> guard;

        /* Construct an empty guard proxy. */
        Guard(RecursiveLock& lock) : lock(lock) {}

        /* Construct the outermost guard proxy for the recursive lock. */
        Guard(RecursiveLock& lock, Wrapped guard) : lock(lock), guard(guard) {}

        /* Disabled copy constructor/assignment for compatibility with
        std::unique_lock. */
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;

        /* Move constructor. */
        Guard(Guard&& other) : lock(other.lock), guard(std::move(other.guard)) {}

        /* Move assignment operator. */
        Guard& operator=(Guard&& other) {
            lock = other.lock;
            guard = std::move(other.guard);
            return *this;
        }

        /* Destroy the guard proxy and reset the lock's owner. */
        ~Guard() {
            if (guard.has_value()) {
                lock.owner = std::thread::id();  // empty id
            }
        }

    };

    /* Acquire the lock, allowing repeated locks within a single thread. */
    template <typename... Args>
    inline Guard operator()(Args&&... args) const {
        auto id = std::this_thread::get_id();

        // if the current thread already owns the lock, return an empty guard
        if (id == owner) {
            return Guard(*this);
        }

        // NOTE: the handling of the `owner` identifier is only thread-safe due to the 
        // exact order of operations here.  As written, the owner identifier is only
        // modified AFTER a lock has been acquired, which guarantees that we will not
        // encounter any race conditions during our comparisons.
        
        // If two threads attempt to acquire the lock at the same time, only the first
        // thread will proceed to modify the owner.  The second just waits until the
        // first thread releases the lock, and then updates the owner accordingly.

        // otherwise, attempt to acquire the lock
        Wrapped guard = Lock::operator()(std::forward<Args>(args)...);  // blocks
        owner = id;
        return Guard(*this, std::move(guard));
    }

};




// TODO: implement Spinlock and TimedLock decorators


// SpinLock could use a protected `try_lock` method that returns a std::optional<Guard>.
// This calls the mutex's `try_lock` method, and if it fails, it returns nullopt.
// Otherwise, it constructs a guard using std::adopt_lock and inserts it into the
// optional.  The decorator's `operator()` method would then loop until it successfully
// acquires the lock, and then unwraps the guard.


// TODO: SpinLock and TimedLock can be combined into a single decorator that handles
// bounded waiting in general.  This would accept the following arguments:

// max_retries: the maximum number of times to cycle the lock before giving up
// -> defaults to std::nullopt (no limit)
// timeout: the maximum amount of time to wait for the lock before giving up
// -> defaults to std::nullopt (no limit)
// wait: the amount of time to wait between lock cycles
// -> defaults to 0 (spinlock)

// These could be supplied as template parameters.


/* A lock decorator that adds a spin effect to lock acquisition. */
template <typename Lock>
class SpinLock : public Lock {
public:

    /* Acquire the lock, spinning for  */


};





/* A lock decorator that adds timeout conditions in the event of contention. */
template <typename Lock, typename Unit = std::chrono::nanoseconds>
class TimedLock {



public:
    using Clock = std::chrono::high_resolution_clock;
    using Resolution = Unit;
};











/* A lock decorator that adds tracks performance diagnostics for the lock. */
template <typename Lock, typename Unit = std::chrono::nanoseconds>
class DiagnosticLock : public Lock {
    mutable size_t lock_count = 0;
    mutable size_t lock_time = 0;

public:
    using Clock = std::chrono::high_resolution_clock;
    using Resolution = Unit;

    /* Track the elapsed time to acquire a lock on the internal mutex. */
    template <typename... Args>
    inline auto operator()(Args&&... args) const {
        auto start = Clock::now();

        // acquire lock
        auto result = Lock::operator()(std::forward<Args>(args)...);

        auto end = Clock::now();
        lock_time += std::chrono::duration_cast<Resolution>(end - start).count();
        ++lock_count;

        // create a guard using the acquired lock
        return result;
    }

    /* Track the elapsed time to acquire a shared lock, if enabled. */
    template <typename... Args>
    inline auto shared(Args&&... args) const
        -> decltype(Lock::shared(std::forward<Args>(args)...))
    {
        auto start = Clock::now();

        // acquire lock
        auto result = Lock::shared(std::forward<Args>(args)...);

        auto end = Clock::now();
        lock_time += std::chrono::duration_cast<Resolution>(end - start).count();
        ++lock_count;

        // create a guard using the acquired lock
        return result;
    }

    /* Get the total number of times the mutex has been locked. */
    inline size_t count() const {
        return lock_count;
    }

    /* Get the total time spent waiting to acquire the lock. */
    inline size_t duration() const {
        return lock_time;  // includes a small overhead for lock acquisition
    }

    /* Get the average time spent waiting to acquire the lock. */
    inline double contention() const {
        return static_cast<double>(lock_time) / lock_count;
    }

    /* Reset the internal diagnostic counters. */
    inline void reset_diagnostics() {
        lock_count = 0;
        lock_time = 0;
    }

};



/* A wrapper around a C++ lock guard that allows it to be used as a Python context
manager. */
template <typename Lock, const std::string_view& name>
class PyLock {
    using Guard = typename Lock::Guard;

    PyObject_HEAD
    const Lock* lock;
    Slot<Guard> guard;

    /* Force users to use init() factory method. */
    PyLock() = delete;
    PyLock(const PyLock&) = delete;
    PyLock(PyLock&&) = delete;

    /* Initialize a PyTypeObject to represent this lock from Python. */
    static PyTypeObject init_type() {
        PyTypeObject type_obj;  // zero-initialize
        type_obj.tp_name = name.data();
        type_obj.tp_doc = "Python-compatible wrapper around a C++ lock guard.";
        type_obj.tp_basicsize = sizeof(PyLock);
        type_obj.tp_flags = Py_TPFLAGS_DEFAULT;
        type_obj.tp_alloc = PyType_GenericAlloc;
        type_obj.tp_new = PyType_GenericNew;
        type_obj.tp_methods = methods;
        type_obj.tp_dealloc = (destructor) dealloc;

        // register iterator type with Python
        if (PyType_Ready(&type_obj) < 0) {
            throw std::runtime_error("could not initialize PyLock type");
        }
        return type_obj;
    }

public:
    /* C-style Python type declaration. */
    inline static PyTypeObject Type = init_type();

    /* Construct a Python lock from a C++ lock guard. */
    inline static PyObject* init(const Lock* lock) {
        // create new iterator instance
        PyLock* result = PyObject_New(PyLock, &Type);
        if (result == nullptr) {
            throw std::runtime_error("could not allocate Python iterator");
        }

        // initialize lock functor
        result->lock = lock;

        // return as PyObject*
        return reinterpret_cast<PyObject*>(result);
    }

    // TODO: SFINAE support for shared() accessor.

    /* Enter the context manager's block, acquiring a new lock. */
    inline static PyObject* enter(PyObject* py_self) {
        PyLock* self = reinterpret_cast<PyLock*>(py_self);
        if (!self->guard.constructed()) {
            self->guard.construct(self->lock->operator()());
        }
        return py_self;
    }

    /* Exit the context manager's block, releasing the lock. */
    inline static PyObject* exit(PyObject* py_self, PyObject* args) {
        PyLock* self = reinterpret_cast<PyLock*>(py_self);
        if (self->guard.constructed()) {
            self->guard.destroy();
        }
        Py_RETURN_NONE;
    }

    /* Check if the lock is acquired. */
    inline static PyObject* locked(PyObject* py_self, PyObject* args) {
        PyLock* self = reinterpret_cast<PyLock*>(py_self);
        return PyBool_FromLong(self->guard.constructed());
    }

    /* Release the lock when the context manager is garbage collected, if it hasn't
    been released already. */
    inline static void dealloc(PyObject* py_self) {
        PyLock* self = reinterpret_cast<PyLock*>(py_self);
        if (self->guard.constructed()) {
            self->guard.destroy();
        }
        // TODO: Type.tp_free(self)  <- we already have access to the type object
        Py_TYPE(py_self)->tp_free(py_self);
    }


private:

    /* Vtable containing Python methods for the context manager. */
    inline static PyMethodDef methods[] = {
        {"__enter__", (PyCFunction) enter, METH_NOARGS, "Enter the context manager."},
        {"__exit__", (PyCFunction) exit, METH_VARARGS, "Exit the context manager."},
        {"locked", (PyCFunction) locked, METH_VARARGS, "Check if the lock is acquired."},
        {NULL}  // sentinel
    };

};



#endif  // BERTRAND_STRUCTS_CORE_THREAD_H include guard
