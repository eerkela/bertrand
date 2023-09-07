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
ThreadLock is a functor (function object) that produces std::lock_guards for an
internal mutex to allow thread-safe operations on a linked list, set, or dictionary.
It also optionally tracks diagnostics on the total number of times the mutex has been
locked and the average contention time for each lock.
*/


/* A functor that produces threading locks for the templated view. */
template <typename ViewType>
class ThreadLock {
public:
    using View = ViewType;
    using Guard = std::lock_guard<std::mutex>;
    using Clock = std::chrono::high_resolution_clock;
    using Resolution = std::chrono::nanoseconds;

    /* Return a std::lock_guard for the internal mutex using RAII semantics.  The mutex
    is automatically acquired when the guard is constructed and released when it goes
    out of scope.  Any operations in between are guaranteed to be atomic. */
    inline Guard operator()() const {
        if (track_diagnostics) {
            auto start = Clock::now();

            // acquire lock
            mtx.lock();

            auto end = Clock::now();
            lock_time += std::chrono::duration_cast<Resolution>(end - start).count();
            ++lock_count;

            // create a guard using the acquired lock
            return Guard(mtx, std::adopt_lock);
        }

        return Guard(mtx);
    }

    /* Return a heap-allocated std::lock_guard for the internal mutex.  The mutex is
    automatically acquired when the guard is constructed and released when it is
    manually deleted.  Any operations in between are guaranteed to be atomic.

    NOTE: this method is generally less safe than using the standard functor operator,
    but can be used for compatibility with Python's context manager protocol. */
    inline Guard* context() const {
        if (track_diagnostics) {
            auto start = Clock::now();

            // acquire lock
            Guard* lock = new Guard(mtx);

            auto end = Clock::now();
            lock_time += std::chrono::duration_cast<Resolution>(end - start).count();
            ++lock_count;

            return lock;
        }

        return new Guard(mtx);
    }

    /* Toggle diagnostics on or off and return its current setting. */
    inline bool diagnostics(std::optional<bool> enabled = std::nullopt) const {
        if (enabled.has_value()) {
            track_diagnostics = enabled.value();
        }
        return track_diagnostics;
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
    inline void reset_diagnostics() const {
        lock_count = 0;
        lock_time = 0;
    }

private:
    friend View;
    mutable std::mutex mtx;
    mutable bool track_diagnostics = false;
    mutable size_t lock_count = 0;
    mutable size_t lock_time = 0;
};


#endif  // BERTRAND_STRUCTS_CORE_THREAD_H include guard
