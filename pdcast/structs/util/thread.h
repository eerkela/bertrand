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
#include "slot.h"  // Slot
#include "string.h"  // PyName


namespace bertrand {
namespace structs {
namespace util {


////////////////////////////////////
////    FORWARD DECLARATIONS    ////
////////////////////////////////////


/* Enum holding all possible guard types. */
enum class Lock {
    EXCLUSIVE,  // returned by functor()  <- default call operator
    SHARED  // returned by functor.shared()
};


template <typename Mutex, Lock mode>
class Guard;


template <typename LockType, typename Guard>
class RecursiveGuard;


class BasicLock;


class ReadWriteLock;


template <typename LockType>
class RecursiveLock;


template <
    typename LockType,
    int MaxRetries,
    typename TimeoutUnit,
    int TimeoutValue,
    typename WaitUnit,
    int WaitValue
>
class SpinLock;


template <typename LockType, typename Unit>
class DiagnosticLock;


template <typename LockType, Lock mode>
class PyLock;


template <typename LockType>
class LockTraits;


//////////////////////
////    GUARDS    ////
//////////////////////


/* The guards presented here are simple wrappers around C++-style `std::lock_guards`
 * and their related classes (std::unique_lock, std::shared_lock, etc.).  They are used
 * in the same way, but are customized to work with the lock functors defined below.
 *
 * Each type of guard specializes the same overall `Guard` interface, which is defined
 * as follows:
 *
 *      template <typename Mutex, Lock mode = ...>
 *      class Guard {
 *      public:
 *          using mutex_type = Mutex;
 *          static constexpr Lock lock_mode = mode;
 *
 *          Guard(mutex_type& mtx);
 *          Guard(mutex_type& mtx, std::adopt_lock_t t);
 *          Guard(Guard&& other);
 *          Guard& operator=(Guard&& other);
 *          void swap(Guard& other);
 *          bool locked();
 *          operator bool();  // equivalent to `locked()`
 *      };
 *
 * This is a narrower interface than the standard lock guards, but it ensures that
 * guards cannot constructed without properly acquiring or transferring ownership of a
 * mutex, and cannot be prematurely locked or unlocked by accident.  Empty guards are
 * not allowed (except in recursive contexts, where construction is controlled by the
 * lock functor itself).  This makes locks safer and more intuitive to use, and
 * prevents common mistakes that can lead to deadlocks and other pitfalls.
 */


/* An exclusive guard for a lock functor (default case). */
template <typename Mutex, Lock mode = Lock::EXCLUSIVE>
class Guard {
    std::unique_lock<Mutex> guard;

public:
    using mutex_type = Mutex;
    static constexpr Lock lock_mode = mode;

    /* Acquire a mutex during construction, locking it. */
    Guard(mutex_type& mtx) : guard(mtx) {}

    /* Acquire a pre-locked mutex. */
    Guard(mutex_type& mtx, std::adopt_lock_t t) : guard(mtx, std::adopt_lock) {}

    /* Move constructor. */
    Guard(Guard&& other) : guard(std::move(other.guard)) {}

    /* Move assignment operator. */
    Guard& operator=(Guard&& other) {
        guard = std::move(other.guard);
        return *this;
    }

    /* Swap state with another Guard. */
    inline void swap(Guard& other) {
        guard.swap(other.guard);
    }

    /* Check if the guard owns a lock on the associated mutex. */
    inline bool locked() const noexcept {
        return guard.owns_lock();
    }

    /* Check if the guard owns a lock on the associated mutex. */
    inline explicit operator bool() const noexcept {
        return locked();
    }

};


/* A shared guard for a lock functor. */
template <typename Mutex>
class Guard<Mutex, Lock::SHARED> {
    std::shared_lock<Mutex> guard;

public:
    using mutex_type = Mutex;
    static constexpr Lock lock_mode = Lock::SHARED;

    /* Acquire a mutex during construction, locking it. */
    Guard(mutex_type& mtx) : guard(mtx) {}

    /* Acquire a pre-locked mutex. */
    Guard(mutex_type& mtx, std::adopt_lock_t t) : guard(mtx, std::adopt_lock) {}

    /* Move constructor. */
    Guard(Guard&& other) : guard(std::move(other.guard)) {}

    /* Move assignment operator. */
    Guard& operator=(Guard&& other) {
        guard = std::move(other.guard);
        return *this;
    }

    /* Swap state with another Guard. */
    inline void swap(Guard& other) {
        guard.swap(other.guard);
    }

    /* Check if the guard owns a lock on the associated mutex. */
    inline bool locked() const noexcept {
        return guard.owns_lock();
    }

    /* Check if the guard owns a lock on the associated mutex. */
    inline explicit operator bool() const noexcept {
        return locked();
    }

};


/* A decorator for the wrapped lock guard that manages the state of a recursive functor
and allows a single thread to hold multiple locks at once. */
template <typename LockType, typename Guard>
class RecursiveGuard {
    LockType& lock;
    std::optional<Guard> guard;

    /* Construct the outermost guard for the recursive lock.
    
    NOTE: recursive lock proxies have to specify RecursiveGuard as a friend class in
    order to access these private constructors. */
    RecursiveGuard(LockType& lock, Guard&& guard) :
        lock(lock), guard(std::move(guard))
    {}

    /* Construct an empty inner guard for a recursive lock. */
    RecursiveGuard(LockType& lock) : lock(lock) {}

public:
    using mutex_type = typename Guard::mutex_type;
    static constexpr Lock lock_mode = Guard::lock_mode;

    /* Acquire a mutex during construction, locking it. */
    RecursiveGuard(LockType& lock, mutex_type& mtx) : lock(lock), guard(mtx) {}

    /* Acquire a pre-locked mutex. */
    RecursiveGuard(LockType& lock, mutex_type& mtx, std::adopt_lock_t t) :
        lock(lock), guard(mtx, std::adopt_lock)
    {}

    /* Move constructor. */
    RecursiveGuard(RecursiveGuard&& other) :
        lock(other.lock), guard(std::move(other.guard))
    {}

    /* Move assignment operator. */
    RecursiveGuard& operator=(RecursiveGuard&& other) {
        lock = other.lock;
        guard = std::move(other.guard);
        return *this;
    }

    /* Destroy the guard proxy and reset the lock's owner. */
    ~RecursiveGuard() {
        if constexpr (lock_mode == Lock::EXCLUSIVE) {
            lock.owner = std::thread::id();  // empty id
        } else if constexpr (lock_mode == Lock::SHARED) {
            lock.shared_owners.erase(std::this_thread::get_id());
        } else {
            static_assert(false, "unrecognized lock mode");
        }
    }

    /* Swap state with another RecursiveGuard. */
    inline void swap(RecursiveGuard& other) {
        guard.swap(other.guard);
    }

    /* Check if the guard owns a lock on the associated mutex. */
    inline bool locked() const noexcept {
        return guard.has_value() && guard.value().locked();
    }

    /* Check if the guard owns a lock on the associated mutex. */
    inline explicit operator bool() const noexcept {
        return locked();
    }

};


///////////////////////
////    MUTEXES    ////
///////////////////////


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


// TODO: These locks should inherit from BaseLock, which provides the requisite
// python() methods using CRTP.  This handles name inference directly.  We might make
// Guard + SharedGuard subclass from their respective lock types.  They can then
// expose python_name as expected.


/* A lock functor that uses a simple mutex and a `std::unique_lock` guard. */
class BasicLock {
public:
    using Mutex = std::mutex;
    using ExclusiveGuard = Guard<Mutex, Lock::EXCLUSIVE>;

    /* Return a std::unique_lock for the internal mutex using RAII semantics.  The
    mutex is automatically acquired when the guard is constructed and released when it
    goes out of scope.  Any operations in between are guaranteed to be atomic. */
    inline ExclusiveGuard operator()() const noexcept {
        return ExclusiveGuard(mtx);
    }

protected:
    mutable Mutex mtx;

    /* Try to acquire the lock without blocking. */
    inline std::optional<ExclusiveGuard> try_lock() const noexcept {
        if (mtx.try_lock()) {
            return ExclusiveGuard(mtx, std::adopt_lock);
        }
        return std::nullopt;
    }

};


/* A lock functor that uses a shared mutex for concurrent reads and exclusive writes. */
class ReadWriteLock {
public:
    using Mutex = std::shared_mutex;
    using ExclusiveGuard = Guard<Mutex, Lock::EXCLUSIVE>;
    using SharedGuard = Guard<Mutex, Lock::SHARED>;

    /* Return a std::unique_lock for the internal mutex using RAII semantics.  The
    mutex is automatically acquired when the guard is constructed and released when it
    goes out of scope.  Any operations in between are guaranteed to be atomic.*/
    inline ExclusiveGuard operator()() const noexcept {
        return ExclusiveGuard(mtx);
    }

    /* Return a wrapper around a std::shared_lock for the internal mutex.

    NOTE: These locks allow concurrent access to the object as long as no exclusive
    guards have been requested.  This is useful for read-only operations that do not
    modify the underlying data structure.  They have the save RAII semantics as the
    normal call operator. */
    inline SharedGuard shared() const {
        return SharedGuard(mtx);
    }

protected:
    mutable Mutex mtx;

    /* Try to acquire an exclusive lock without blocking. */
    inline std::optional<ExclusiveGuard> try_lock() const noexcept {
        if (mtx.try_lock()) {
            return ExclusiveGuard(mtx, std::adopt_lock);
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
template <typename LockType, bool is_shared = false>
class _RecursiveLock : public LockType {};


/* Base class specialization for shared lock functors. */
template <typename LockType>
class _RecursiveLock<LockType, true> : public LockType {
    using _SharedGuard = typename LockType::SharedGuard;
    mutable std::unordered_set<std::thread::id> shared_owners;

    template <typename _LockType, typename _Guard>
    friend class RecursiveGuard;

public:
    using SharedGuard = RecursiveGuard<RecursiveLock<LockType>, _SharedGuard>;

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
        _SharedGuard guard = LockType::shared(std::forward<Args>(args)...);  // blocks
        shared_owners.insert(id);
        return SharedGuard(*this, std::move(guard));
    }

};


/* A lock decorator that allows a thread to be locked recursively without
deadlocking. */
template <typename LockType>
class RecursiveLock : public _RecursiveLock<LockType, LockType::is_shared> {
    using _ExclusiveGuard = typename LockType::ExclusiveGuard;
    mutable std::thread::id owner;

    template <typename _LockType, typename _Guard>
    friend class RecursiveGuard;

public:
    using ExclusiveGuard = RecursiveGuard<RecursiveLock, _ExclusiveGuard>;

    /* Acquire the lock in exclusive mode, allowing repeated locks within a single
    thread. */
    template <typename... Args>
    inline ExclusiveGuard operator()(Args&&... args) const {
        auto id = std::this_thread::get_id();

        // if the current thread already owns the lock, return an empty guard
        if (id == owner) {
            return ExclusiveGuard(*this);
        }

        // NOTE: the handling of the `owner` identifier is only thread-safe due to the 
        // exact order of operations here.  As written, the owner identifier is only
        // modified AFTER a lock has been acquired, which guarantees that we will not
        // encounter any race conditions during our comparisons.
        
        // If two threads attempt to acquire the lock at the same time, only the first
        // thread will proceed to modify the owner.  The second just waits until the
        // first thread releases the lock, and then updates the owner accordingly.

        // otherwise, attempt to acquire the lock
        _ExclusiveGuard guard = LockType::operator()(std::forward<Args>(args)...);  // blocks
        owner = id;
        return ExclusiveGuard(*this, std::move(guard));
    }

};


/* A lock decorator that adds a spin effect to lock acquisition. */
template <
    typename LockType,
    int MaxRetries = -1,
    typename TimeoutUnit = std::chrono::nanoseconds,
    int TimeoutValue = -1,
    typename WaitUnit = std::chrono::nanoseconds,
    int WaitValue = -1
>
class SpinLock : public LockType {
    using Clock = std::chrono::high_resolution_clock;

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
        -> decltype(LockType::operator()(std::forward<Args>(args)...))
    {
        using Guard = decltype(LockType::operator()(std::forward<Args>(args)...));
        auto end = Clock::now() + timeout;
        int retries = 0;

        // loop until the lock is acquired or we hit an error condition
        while (true) {
            // try to lock the mutex
            std::optional<Guard> guard = LockType::try_lock();
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
                if (Clock::now() > end) {
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
        -> decltype(LockType::shared(std::forward<Args>(args)...))
    {
        using Guard = decltype(LockType::shared(std::forward<Args>(args)...));
        auto end = Clock::now() + timeout;
        int retries = 0;

        // loop until the lock is acquired or we hit an error condition
        while (true) {
            // try to lock the mutex
            std::optional<Guard> guard = LockType::try_shared();
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
                if (Clock::now() > end) {
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
template <typename LockType, typename Unit = std::chrono::nanoseconds>
class DiagnosticLock : public LockType {
    using Clock = std::chrono::high_resolution_clock;
    using Resolution = std::chrono::duration<double, typename Unit::period>;
    mutable size_t lock_count = 0;
    mutable Resolution lock_time = {};

public:

    /* Track the elapsed time to acquire a lock on the internal mutex. */
    template <typename... Args>
    inline auto operator()(Args&&... args) const {
        auto start = Clock::now();

        // acquire lock
        auto result = LockType::operator()(std::forward<Args>(args)...);

        auto end = Clock::now();
        lock_time += std::chrono::duration_cast<Resolution>(end - start);
        ++lock_count;

        // create a guard using the acquired lock
        return result;
    }

    /* Track the elapsed time to acquire a shared lock, if enabled. */
    template <typename... Args>
    inline auto shared(Args&&... args) const
        -> decltype(LockType::shared(std::forward<Args>(args)...))
    {
        auto start = Clock::now();

        // acquire lock
        auto result = LockType::shared(std::forward<Args>(args)...);

        auto end = Clock::now();
        lock_time += std::chrono::duration_cast<Resolution>(end - start);
        ++lock_count;

        // create a guard using the acquired lock
        return result;
    }

    /* Get the total number of times the mutex has been locked. */
    inline size_t count() const {
        return lock_count;
    }

    /* Get the total time spent waiting to acquire the lock. */
    inline Resolution duration() const {
        return lock_time;  // includes a small overhead for lock acquisition
    }

    /* Get the average time spent waiting to acquire the lock. */
    inline Resolution contention() const {
        return (lock_count == 0) ? Resolution(0) : (lock_time / lock_count);
    }

    /* Reset the internal diagnostic counters. */
    inline void reset_diagnostics() {
        lock_count = 0;
        lock_time = {};
    }

};


/* A wrapper around a C++ lock guard that allows it to be used as a Python context
manager. */
template <typename LockType, Lock mode>
class PyLock {
    using Guard = typename LockType::Guard;

    template <bool cond = shared, std::enable_if_t<cond, int> = 0>
    using SharedGuard = typename LockType::SharedGuard;  // may not exist

    PyObject_HEAD
    std::conditional_t<shared, Slot<SharedGuard>, Slot<Guard>> guard;
    const LockType* lock;

    /* Force users to use init() factory method. */
    PyLock() = delete;
    PyLock(const PyLock&) = delete;
    PyLock(PyLock&&) = delete;

public:

    /* Construct a Python lock from a C++ lock guard. */
    inline static PyObject* init(const LockType* lock) {
        // create new iterator instance
        PyLock* result = PyObject_New(PyLock, &Type);
        if (result == nullptr) {
            throw std::runtime_error("could not allocate Python iterator");
        }

        // initialize (NOTE: PyObject_New() does not call stack constructors)
        if constexpr (shared) {
            new (&(result->guard)) Slot<SharedGuard>();
        } else {
            new (&(result->guard)) Slot<Guard>();
        };
        result->lock = lock;

        // return as PyObject*
        return reinterpret_cast<PyObject*>(result);
    }

    // TODO: SFINAE support for shared() accessor.

    /* Enter the context manager's block, acquiring a new lock. */
    inline static PyObject* enter(PyLock* self, PyObject* args) {
        self->guard.construct(self->lock->operator()());
        return Py_NewRef(self);
    }

    /* Exit the context manager's block, releasing the lock. */
    inline static PyObject* exit(PyLock* self, PyObject* args) {
        self->guard.destroy();
        Py_DECREF(self);
        Py_RETURN_NONE;
    }

    /* Check if the lock is acquired. */
    inline static PyObject* locked(PyLock* self, PyObject* args) {
        return PyBool_FromLong(self->guard.constructed());
    }

    /* Release the lock when the context manager is garbage collected, if it hasn't
    been released already. */
    inline static void dealloc(PyLock* self) {
        self->guard.destroy();
        Type.tp_free(self);
    }

private:

    /* Vtable containing Python methods for the context manager. */
    inline static PyMethodDef methods[4] = {
        {"__enter__", (PyCFunction) enter, METH_NOARGS, "Enter the context manager."},
        {"__exit__", (PyCFunction) exit, METH_VARARGS, "Exit the context manager."},
        {"locked", (PyCFunction) locked, METH_NOARGS, "Check if the lock is acquired."},
        {NULL}  // sentinel
    };

    /* Initialize a PyTypeObject to represent this lock from Python. */
    static PyTypeObject init_type() {
        PyTypeObject type_obj;  // zero-initialize
        if constexpr (shared) {
            type_obj.tp_name = PyName<SharedGuard>.data();
        } else {
            type_obj.tp_name = PyName<Guard>.data();
        }
        type_obj.tp_doc = "Python-compatible wrapper around a C++ lock guard.";
        type_obj.tp_basicsize = sizeof(PyLock);
        type_obj.tp_flags = (
            Py_TPFLAGS_DEFAULT | Py_TPFLAGS_IMMUTABLETYPE |
            Py_TPFLAGS_DISALLOW_INSTANTIATION
        );
        type_obj.tp_alloc = PyType_GenericAlloc;
        type_obj.tp_methods = methods;
        type_obj.tp_dealloc = (destructor) dealloc;

        // register iterator type with Python
        if (PyType_Ready(&type_obj) < 0) {
            throw std::runtime_error("could not initialize PyLock type");
        }
        return type_obj;
    }

    /* C-style Python type declaration. */
    inline static PyTypeObject Type = init_type();
};


//////////////////////
////    TRAITS    ////
//////////////////////


/* A collection of SFINAE traits for inspecting lock types at compile time. */
template <typename LockType>
class LockTraits {

    /* Detects whether the templated lock has a shared() method, indicating a
    read/write locking strategy. */
    struct _is_shared {
        template <typename T>
        static constexpr auto test(T* t) -> decltype(t->shared(), std::true_type());
        template <typename T>
        static constexpr auto test(...) -> std::false_type;
        static constexpr bool value = decltype(test<LockType>(nullptr))::value;
    };

    /* Detects whether the templated lock inherits from Recursive<>, allowing it to
    be locked/unlocked recursively within a single thread. */
    struct _is_recursive {
        using Recursive = RecursiveLock<LockType>;
        static constexpr bool value = std::is_base_of_v<Recursive, LockType>;
    };

    /* Get the unit used for diagnostic durations. */
    struct _diagnostic_unit {
        template <typename T>
        static constexpr auto test(T* t) -> decltype(t->contention());
        template <typename T>
        static constexpr auto test(...) -> void;
        using type = decltype(test<LockType>(nullptr));
    };

    /* Get the maximum number of retries for the lock, if it has a corresponding
    static member (defaults to -1 otherwise, indicating unlimited retries). */
    struct _max_retries {
        template <typename T, int max_retries = T::max_retries>
        static constexpr auto test(int) -> std::integral_constant<int, max_retries>;
        template <typename T>
        static constexpr auto test(...) -> std::integral_constant<int, -1>;
        static constexpr int value = decltype(test<LockType>(0))::value;
    };

    /* Get the timeout duration for the lock, if it has a corresponding static member
    (defaults to -1ns otherwise, indicating an unlimited timeout duration). */
    struct _timeout {
        template <typename T>
        static constexpr auto test() -> decltype(T::timeout) {
            return T::timeout;
        }
        template <typename T>
        static constexpr auto test(...) -> std::chrono::nanoseconds {
            return std::chrono::nanoseconds(-1);
        }
        using type = decltype(test<LockType>());
        static constexpr type value = test<LockType>();
    };

    /* Get the wait duration for the lock, if it has a corresponding static member
    (defaults to -1ns otherwise, indicating a busy wait cycle). */
    struct _wait {
        template <typename T>
        static constexpr auto test() -> decltype(T::wait) {
            return T::wait;
        }
        template <typename T>
        static constexpr auto test(...) -> std::chrono::nanoseconds {
            return std::chrono::nanoseconds(-1);
        }
        using type = decltype(test<LockType>());
        static constexpr type value = test<LockType>();
    };

public:
    using DiagnosticUnit = typename _diagnostic_unit::type;
    using TimeoutUnit = typename _timeout::type;
    using WaitUnit = typename _wait::type;

    static constexpr bool is_shared = _is_shared::value;
    static constexpr bool is_recursive = _is_recursive::value;
    static constexpr bool is_diagnostic = !std::is_same_v<DiagnosticUnit, void>;
    static constexpr int max_retries = _max_retries::value;
    static constexpr TimeoutUnit timeout = _timeout::value;
    static constexpr WaitUnit wait = _wait::value;
};


}  // namespace util
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_CORE_THREAD_H include guard
