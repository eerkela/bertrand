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

#include "../linked/util.h"  // Slot


////////////////////////
////    FUNCTORS    ////
////////////////////////


/* Locks are functors (function objects) that produce RAII lock guards for an internal
 * mutex.  They can be applied to any data structure, and are significantly safer than
 * manually locking and unlocking a mutex directly.  Locks come in 2 basic flavors:
 * BasicLock and ReadWriteLock.
 * 
 * BasicLock is a simple mutex that allows only one thread to access the data structure
 * at a time.  This has very low overhead and is the default lock type for most data
 * structures.
 * 
 * ReadWriteLock, on the other hand, is capable of producing 2 different types of
 * locks: one which allows multiple threads to access the data structure concurrently
 * (a shared lock - typically used for read-only operations), and one which forces
 * exclusive access just like BasicLock (an exclusive lock - typically used for
 * operations that can modify the underlying data structure).  The object can have
 * several shared locks at once, but only one exclusive lock at a time.  The two types
 * of locks are mutually exclusive, meaning that once a shared lock has been acquired,
 * no other threads can acquire an exclusive lock until all the shared locks have been
 * released.  Similarly, once an exclusive lock is acquired, no other threads can
 * acquire a shared lock until the exclusive lock falls out of context.
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

    /* Try to acquire the lock without blocking. */
    inline std::optional<Guard> try_lock() const noexcept {
        if (mtx.try_lock()) {
            return Guard(mtx, std::adopt_lock);
        }
        return std::nullopt;
    }

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

    /* Try to acquire an exclusive lock without blocking. */
    inline std::optional<Guard> try_lock() const noexcept {
        if (mtx.try_lock()) {
            return Guard(mtx, std::adopt_lock);
        }
        return std::nullopt;
    }

    /* Try to acquire a shared lock without blocking. */
    inline std::optional<SharedGuard> try_shared() const noexcept {
        if (mtx.try_lock_shared()) {
            return SharedGuard(mtx, std::adopt_lock);
        }
        return std::nullopt;
    }

};


//////////////////////////
////    DECORATORS    ////
//////////////////////////


/* Lock decorators are wrappers around one of the core lock functors that add extra
 * functionality.  They can be used to add recursive locking, spinlocks, timed locks,
 * and performance diagnostics to any lock functor.
 *
 * RecursiveLock is a decorator that allows a single thread to acquire the same lock
 * multiple times without deadlocking.  Normally, if a thread attempts to acquire a
 * lock on a mutex that it already owns (within a nested context, for example), the
 * program will deadlock.  Recursive (or reentrant) locks prevent this by tracking the
 * current owner of the mutex and skipping lock acquisition if it references the
 * current thread.  This is useful for recursive functions that need to acquire a lock
 * (either shared or exlusive) on the same mutex multiple times, but is generally
 * discouraged due to the extra overhead involved.  Instead, it is better to refactor
 * the code to avoid recursive locks if possible.  They are provided here for
 * completeness, and in the rare cases that no other solution is possible.
 *
 * SpinLock allows a thread to repeatedly attempt to acquire a lock until it succeeds.
 * This is useful for situations where the lock is expected to be released quickly, but
 * may not succeed on the first try.  By default, a SpinLock will busy wait until the
 * lock is acquired, but this can be modified to introduce a sleep interval between
 * attempts to reduce CPU usage.  A maximum number of retries can also be specified to
 * prevent infinite loops.  SpinLocks can also be used to implement a timed lock, which
 * will attempt to acquire the lock within a specified time limit before giving up.
 * 
 * DiagnosticLock tracks performance characteristics for a lock functor.  These include
 * the total number of times the mutex has been locked and the average waiting time for
 * each lock.  This is useful for profiling the performance of threaded code and
 * identifying potential bottlenecks.
 *
 * Lastly, PyLock is an adapter for a C++ lock guard that allows it to be used as a
 * Python context manager.  This is how locks are exposed to Python code, and allows
 * the use of idiomatic `with` blocks to acquire and release locks.  A lock will be
 * acquired as soon as the context is entered, and released when the context is exited.
 */


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


/* A lock decorator that adds a spin effect to lock acquisition. */
template <
    typename Lock,
    int MaxRetries = -1,
    typename TimeoutUnit = std::chrono::milliseconds,
    int TimeoutValue = -1,
    typename WaitUnit = std::chrono::milliseconds,
    int WaitValue = -1
>
class SpinLock : public Lock {

    /* Guards to ensure that time units are compatible with std::chrono::duration. */
    template <typename T>
    struct is_chrono_duration {
        static constexpr bool value = false;
    };
    template <typename Rep, typename Period>
    struct is_chrono_duration<std::chrono::duration<Rep, Period>> {
        static constexpr bool value = true;
    };

    /* Check that units are given as std::chrono durations. */
    static_assert(
        is_chrono_duration<TimeoutUnit>::value,
        "TimeoutUnit must be a std::chrono duration"
    );
    static_assert(
        is_chrono_duration<WaitUnit>::value,
        "WaitUnit must be a std::chrono duration"
    );

    /* Convert a duration into a numeric string with units. */
    template <typename Duration>
    static std::string duration_to_string(Duration duration) {
        using namespace std::chrono;
        std::ostringstream msg;

        // get numeric component
        msg << duration.count();

        // add units
        if constexpr (std::is_same_v<Duration, nanoseconds>) {
            msg << "ns";
        } else if constexpr (std::is_same_v<Duration, microseconds>) {
            msg << "us";
        } else if constexpr (std::is_same_v<Duration, milliseconds>) {
            msg << "ms";
        } else if constexpr (std::is_same_v<Duration, seconds>) {
            msg << "s";
        } else if constexpr (std::is_same_v<Duration, minutes>) {
            msg << "m";
        } else if constexpr (std::is_same_v<Duration, hours>) {
            msg << "h";
        } else {
            msg << "(" << std::to_string(Duration::period::num) << "/";
            msg << std::to_string(Duration::period::den) << " s)";
        }

        // return as std::string
        return msg.str();
    }

public:
    static constexpr int max_retries = MaxRetries;
    static constexpr TimeoutUnit timeout = TimeoutUnit(TimeoutValue);
    static constexpr WaitUnit wait = WaitUnit(WaitValue);

    /* Acquire an exclusive lock, spinning according to the template parameters. */
    template <typename... Args>
    inline auto operator()(Args&&... args) const
        -> decltype(Lock::operator()(std::forward<Args>(args)...))
    {
        auto end = std::chrono::high_resolution_clock::now() + timeout;
        int retries = 0;

        // loop until the lock is acquired or we hit an error condition
        while (true) {
            // try to lock the mutex
            std::optional<typename Lock::Guard> guard = Lock::try_lock();
            if (guard.has_value()) {
                return guard.value();  // lock succeeded
            }

            // check for maximum number of retries
            if constexpr (max_retries >= 0) {
                if (++retries >= max_retries) {
                    std::ostringstream msg;
                    msg << "failed to acquire exclusive lock (exceeded max retries: ";
                    msg << max_retries << ")";
                    throw std::runtime_error(msg.str());
                }
            }

            // check for timeout
            if constexpr (TimeoutValue >= 0) {
                if (std::chrono::high_resolution_clock::now() > end) {
                    std::ostringstream msg;
                    msg << "failed to acquire exclusive lock (exceeded timeout: ";
                    msg << duration_to_string(timeout) << ")";
                    throw std::runtime_error(msg.str());
                }
            }

            // wait and try again
            if constexpr (WaitValue > 0) {
                std::this_thread::sleep_for(wait);
            }
        }
        // indefinite loop
    }

    /* Acquire a shared lock, spinning according to the template parameters. */
    template <typename... Args>
    inline auto shared(Args&&... args) const
        -> decltype(Lock::shared(std::forward<Args>(args)...))
    {
        auto end = std::chrono::high_resolution_clock::now() + timeout;
        int retries = 0;

        // loop until the lock is acquired or we hit an error condition
        while (true) {
            // try to lock the mutex
            std::optional<typename Lock::SharedGuard> guard = Lock::try_shared();
            if (guard.has_value()) {
                return guard.value();  // lock succeeded
            }

            // check for maximum number of retries
            if constexpr (max_retries > 0) {
                if (++retries >= max_retries) {
                    std::ostringstream msg;
                    msg << "failed to acquire shared lock (exceeded max retries: ";
                    msg << max_retries << ")";
                    throw std::runtime_error(msg.str());
                }
            }

            // check for timeout
            if constexpr (TimeoutValue > 0) {
                if (std::chrono::high_resolution_clock::now() > end) {
                    std::ostringstream msg;
                    msg << "failed to acquire shared lock (exceeded timeout: ";
                    msg << duration_to_string(timeout) << ")";
                    throw std::runtime_error(msg.str());
                }
            }

            // wait and try again
            if constexpr (WaitValue > 0) {
                std::this_thread::sleep_for(wait);
            }
        }
        // indefinite loop
    }

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
    PyObject_HEAD
    const Lock* lock;
    Slot<typename Lock::Guard> guard;

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
        type_obj.tp_dealloc = dealloc;

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

    // TODO: context manager sometimes segfaults when exiting context block for some
    // reason.  This doesn't seem to happen all the time, though.

    /* Exit the context manager's block, releasing the lock. */
    inline static PyObject* exit(PyObject* py_self, PyObject* args) {
        PyLock* self = reinterpret_cast<PyLock*>(py_self);
        if (self->guard.constructed()) {
            // TODO: segfault seems to happen here.
            self->guard.destroy();  // something wrong with slot.destroy?
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
        Type.tp_free(py_self);
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
