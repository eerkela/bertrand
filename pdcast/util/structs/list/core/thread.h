// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_THREAD_H
#define BERTRAND_STRUCTS_CORE_THREAD_H

#include <chrono>  // std::chrono
#include <mutex>  // std::mutex, std::lock_guard
#include <optional>  // std::optional


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
    using Guard = std::lock_guard<std::mutex>;
    using Clock = std::chrono::high_resolution_clock;
    using Resolution = std::chrono::nanoseconds;

    /* Return a std::lock_guard for the internal mutex using RAII semantics.  The mutex
    is automatically acquired when the guard is constructed and released when it goes
    out of scope.  Any operations in between are guaranteed to be atomic. */
    inline Guard operator()() const {
        return Guard(mtx);
    }

    /* Return a heap-allocated std::lock_guard for the internal mutex.  The mutex is
    automatically acquired when the guard is constructed and released when it is
    manually deleted.  Any operations in between are guaranteed to be atomic.

    NOTE: this method is generally less safe than using the standard functor operator,
    but can be used for compatibility with Python's context manager protocol. */
    inline Guard* context() const {
        return new Guard(mtx);
    }

protected:
    mutable std::mutex mtx;
};


/* A functor that also tracks performance diagnostics for the lock. */
class DiagnosticLock : public BasicLock {
public:
    using Guard = typename BasicLock::Guard;
    using Clock = typename BasicLock::Clock;
    using Resolution = typename BasicLock::Resolution;

    /* Track the elapsed time to acquire a lock guard for the mutex. */
    inline Guard operator()() const {
        auto start = Clock::now();

        // acquire lock
        this->mtx.lock();

        auto end = Clock::now();
        lock_time += std::chrono::duration_cast<Resolution>(end - start).count();
        ++lock_count;

        // create a guard using the acquired lock
        return Guard(this->mtx, std::adopt_lock);
    }

    /* Track the elapsed time to acquire a heap-allocated lock guard for the mutex. */
    inline Guard* context() const {
        auto start = Clock::now();

        // acquire lock
        Guard* lock = new Guard(this->mtx);

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
