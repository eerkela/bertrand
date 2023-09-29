// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_THREAD_H
#define BERTRAND_STRUCTS_CORE_THREAD_H

#include <chrono>  // std::chrono
#include <memory>  // std::shared_ptr, std::weak_ptr
#include <mutex>  // std::mutex, std::lock_guard, std::unique_lock
#include <optional>  // std::optional
#include <shared_mutex>  // std::shared_mutex, std::shared_lock
// #include <thread>  // std::thread
#include <unordered_map>  // std::unordered_map


// TODO: implement a ReadWriteLock that allows concurrent reads and exclusive writes.
// This needs to handle recursive locking, wherein a thread locks the mutex multiple
// times within a nested scope.


////////////////////////
////    FUNCTORS    ////
////////////////////////


/*
Locks are functors (function objects) that produce std::lock_guards for an internal
mutex.  They can be applied to any data structure, and are generally safer than
manually locking and unlocking the mutex.

DiagnosticLock is a variation of BasicLock that provides the same functionality, but
also keeps track of basic diagnostics, including the total number of times the mutex
has been locked and the average contention time for each lock.  This is useful for
profiling the performance of threaded code and identifying potential bottlenecks.
*/


/* A functor that produces threading locks for an internal mutex. */
class BasicLock {
public:
    using Lock = std::lock_guard<std::recursive_mutex>;

    /* Return a std::lock_guard for the internal mutex using RAII semantics.  The mutex
    is automatically acquired when the guard is constructed and released when it goes
    out of scope.  Any operations in between are guaranteed to be atomic. */
    inline Lock operator()() const {
        return Lock(mtx);
    }

    /* Return a heap-allocated std::lock_guard for the internal mutex.  The mutex is
    automatically acquired when the guard is constructed and released when it is
    manually deleted.  Any operations in between are guaranteed to be atomic.

    NOTE: this method is generally less safe than using the standard functor operator,
    but can be used for compatibility with Python's context manager protocol. */
    inline Lock* context() const {
        return new Lock(mtx);
    }

protected:
    mutable std::recursive_mutex mtx;
};






/* A functor that produces read/write thread locks for an internal mutex. */
class ReadWriteLock {
public:
    using SharedLock = std::shared_lock<std::shared_mutex>;
    using UniqueLock = std::unique_lock<std::shared_mutex>;

    /* Return a std::unique_lock for the internal mutex.

    NOTE: The lock is shared amongst all references within a single thread, and is
    automatically released when the last reference goes out of scope.  If a lock has
    already been generated for the current thread, we simple return a new shared_ptr
    to the existing lock.  This avoids contention within a thread while still blocking
    the mutex against concurrent access between threads. */
    inline std::shared_ptr<UniqueLock> operator()() const {
        // check for existing lock
        auto iter = unique_lock.find(instance_id);
        if (iter != unique_lock.end()) {
            return iter->second;
        }

        // create a new lock
        auto lock = std::shared_ptr<UniqueLock>(
            new UniqueLock(mtx), 
            [this](UniqueLock* lock) {
                unique_lock.erase(instance_id);
                delete lock;
            }
        );

        // store the lock in the thread-local map
        unique_lock[instance_id] = lock;
        return lock;
    }

    /* Return a std::shared_lock for the internal mutex.

    NOTE: These locks allow concurrent access to the object as long as no UniqueLocks
    have been requested.  This is useful for read-only operations that do not modify
    the underlying data structure. They have the save RAII semantics as the unique
    locks. */
    inline std::shared_ptr<SharedLock> shared() const {
        // check for existing lock
        auto iter = shared_lock.find(instance_id);
        if (iter != shared_lock.end()) {
            return iter->second;
        }

        // create a new lock
        auto lock = std::shared_ptr<SharedLock>(
            new SharedLock(mtx), 
            [this](SharedLock* lock) {
                shared_lock.erase(instance_id);
                delete lock;
            }
        );

        // store the lock in the thread-local map
        shared_lock[instance_id] = lock;
        return lock;
    }


    // TODO: Python context managers should use a shared_ptr to a lock



    /* Return a heap-allocated std::unique_lock for the internal mutex, which must be
    manually deleted to release the lock.

    NOTE: this method is generally less safe than using the standard functor operator,
    but can be used for compatibility with Python's context manager protocol. */
    inline UniqueLock* context() const {
        return new UniqueLock(mtx);
    }

    /* Return a heap-allocated std::shared_lock for the internal mutex, which must be
    manually deleted to release the lock.

    NOTE: the same caveats apply as for the ordinary context() method. */
    inline SharedLock* shared_context() const {
        return new SharedLock(mtx);
    }

protected:
    const uintptr_t instance_id = reinterpret_cast<uintptr_t>(this);
    mutable std::shared_mutex mtx;

    template <typename LockType>
    using Locks = std::unordered_map<uintptr_t, std::shared_ptr<LockType>>;

    static thread_local Locks<UniqueLock> unique_lock;
    static thread_local Locks<SharedLock> shared_lock;
};




/* A functor that also tracks performance diagnostics for the lock. */
class DiagnosticLock : public BasicLock {
public:
    using Lock = typename BasicLock::Lock;
    using Clock = std::chrono::high_resolution_clock;
    using Resolution = std::chrono::nanoseconds;

    /* Track the elapsed time to acquire a lock guard for the mutex. */
    inline Lock operator()() const {
        auto start = Clock::now();

        // acquire lock
        this->mtx.lock();

        auto end = Clock::now();
        lock_time += std::chrono::duration_cast<Resolution>(end - start).count();
        ++lock_count;

        // create a guard using the acquired lock
        return Lock(this->mtx, std::adopt_lock);
    }

    /* Track the elapsed time to acquire a heap-allocated lock guard for the mutex. */
    inline Lock* context() const {
        auto start = Clock::now();

        // acquire lock
        Lock* lock = new Lock(this->mtx);

        auto end = Clock::now();
        lock_time += std::chrono::duration_cast<Resolution>(end - start).count();
        ++lock_count;

        return lock;
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

private:
    mutable size_t lock_count = 0;
    mutable size_t lock_time = 0;
};


#endif  // BERTRAND_STRUCTS_CORE_THREAD_H include guard
