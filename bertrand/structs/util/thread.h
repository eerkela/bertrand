#ifndef BERTRAND_STRUCTS_UTIL_THREAD_H
#define BERTRAND_STRUCTS_UTIL_THREAD_H

#include <chrono>  // std::chrono
#include <memory>  // std::shared_ptr, std::weak_ptr
#include <mutex>  // std::mutex, std::lock_guard, std::unique_lock
#include <optional>  // std::optional
#include <shared_mutex>  // std::shared_mutex, std::shared_lock
#include <sstream>  // std::ostringstream
#include <string_view>  // std::string_view
#include <thread>  // std::thread
#include <type_traits>  // std::is_same_v, std::is_base_of_v, std::enable_if_t, etc.
#include <unordered_set>  // std::unordered_set
#include "args.h"  // PyArgs, CallProtocol
#include "except.h"  // throw_python()
#include "string.h"  // PyName


namespace bertrand {
namespace util {


////////////////////////////////////
////    FORWARD DECLARATIONS    ////
////////////////////////////////////


/* Enum holding all possible guard types. */
enum class LockMode {
    EXCLUSIVE,
    SHARED
};


template <typename Mutex, LockMode lock_mode>
class ThreadGuard;
template <typename GuardType, typename LockType>
class RecursiveThreadGuard;
template <typename GuardType, typename LockType>
class PyThreadGuard;
template <typename GuardType>
class ThreadGuardTraits;
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
template <typename LockType>
class PyLock;
template <typename LockType>
class LockTraits;


/////////////////////////////
////    THREAD GUARDS    ////
/////////////////////////////


/* The guards presented here are simple wrappers around C++-style `std::lock_guards`
 * and their related classes (std::unique_lock, std::shared_lock, etc.).  They are used
 * in the same way, but are customized to work with the lock functors defined below.
 *
 * Each type of guard specializes the same overall `ThreadGuard` interface, which is
 * defined as follows:
 *
 *      template <typename Mutex, Lock mode = ...>
 *      class ThreadGuard {
 *      public:
 *          using mutex_type = Mutex;
 *          static constexpr Lock lock_mode = mode;
 *
 *          ThreadGuard(mutex_type& mtx);
 *          ThreadGuard(mutex_type& mtx, std::adopt_lock_t t);
 *          ThreadGuard(ThreadGuard&& other);
 *          ThreadGuard& operator=(ThreadGuard&& other);
 *          void swap(ThreadGuard& other);
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


/* Empty type inherited by every custom lock guard.  This makes for easy assertions
during SFINAE trait lookups. */ 
class ThreadGuardTag {};


/* An exclusive guard for a lock functor (default case). */
template <typename Mutex, LockMode lock_mode = LockMode::EXCLUSIVE>
class ThreadGuard : ThreadGuardTag {
    std::unique_lock<Mutex> guard;

public:
    using mutex_type = Mutex;
    static constexpr LockMode mode = lock_mode;
    static constexpr bool recursive = false;

    /* Acquire a mutex during construction, locking it. */
    ThreadGuard(mutex_type& mtx) : guard(mtx) {}

    /* Acquire a pre-locked mutex. */
    ThreadGuard(mutex_type& mtx, std::adopt_lock_t t) : guard(mtx, std::adopt_lock) {}

    /* Move constructor. */
    ThreadGuard(ThreadGuard&& other) : guard(std::move(other.guard)) {}

    /* Move assignment operator. */
    ThreadGuard& operator=(ThreadGuard&& other) {
        guard = std::move(other.guard);
        return *this;
    }

    /* Swap state with another Guard. */
    inline void swap(ThreadGuard& other) {
        guard.swap(other.guard);
    }

    /* Check if the guard owns a lock on the associated mutex. */
    inline bool active() const noexcept {
        return guard.owns_lock();
    }

    /* Check if the guard owns a lock on the associated mutex. */
    inline explicit operator bool() const noexcept {
        return active();
    }

};


/* A shared guard for a lock functor. */
template <typename Mutex>
class ThreadGuard<Mutex, LockMode::SHARED> : ThreadGuardTag {
    std::shared_lock<Mutex> guard;

public:
    using mutex_type = Mutex;
    static constexpr LockMode mode = LockMode::SHARED;
    static constexpr bool recursive = false;

    /* Acquire a mutex during construction, locking it. */
    ThreadGuard(mutex_type& mtx) : guard(mtx) {}

    /* Acquire a pre-locked mutex. */
    ThreadGuard(mutex_type& mtx, std::adopt_lock_t t) : guard(mtx, std::adopt_lock) {}

    /* Move constructor. */
    ThreadGuard(ThreadGuard&& other) : guard(std::move(other.guard)) {}

    /* Move assignment operator. */
    ThreadGuard& operator=(ThreadGuard&& other) {
        guard = std::move(other.guard);
        return *this;
    }

    /* Swap state with another ThreadGuard. */
    inline void swap(ThreadGuard& other) {
        guard.swap(other.guard);
    }

    /* Check if the guard owns a lock on the associated mutex. */
    inline bool active() const noexcept {
        return guard.owns_lock();
    }

    /* Check if the guard owns a lock on the associated mutex. */
    inline explicit operator bool() const noexcept {
        return active();
    }

};


/* A decorator for the wrapped guard that manages the state of a recursive lock functor
and allows a single thread to hold multiple locks at once. */
template <typename GuardType, typename LockType>
class RecursiveThreadGuard : ThreadGuardTag {
    LockType& lock;
    std::optional<GuardType> guard;

    /* Construct the outermost guard for the recursive lock. */
    RecursiveThreadGuard(LockType& lock, GuardType&& guard) :
        lock(lock), guard(std::move(guard))
    {}

    /* Construct an empty inner guard for a recursive lock. */
    RecursiveThreadGuard(LockType& lock) : lock(lock) {}

public:
    using mutex_type = typename GuardType::mutex_type;
    static constexpr LockMode mode = GuardType::mode;
    static constexpr bool recursive = true;

    /* Acquire a mutex during construction, locking it. */
    RecursiveThreadGuard(LockType& lock, mutex_type& mtx) : lock(lock), guard(mtx) {}

    /* Acquire a pre-locked mutex. */
    RecursiveThreadGuard(LockType& lock, mutex_type& mtx, std::adopt_lock_t t) :
        lock(lock), guard(mtx, std::adopt_lock)
    {}

    /* Move constructor. */
    RecursiveThreadGuard(RecursiveThreadGuard&& other) :
        lock(other.lock), guard(std::move(other.guard))
    {}

    /* Move assignment operator. */
    RecursiveThreadGuard& operator=(RecursiveThreadGuard&& other) {
        lock = other.lock;
        guard = std::move(other.guard);
        return *this;
    }

    /* Destroy the guard proxy and reset the lock's owner. */
    ~RecursiveThreadGuard() {
        static_assert(
            mode == LockMode::EXCLUSIVE || mode == LockMode::SHARED,
            "unrecognized lock mode"
        );
        if constexpr (mode == LockMode::EXCLUSIVE) {
            lock.owner = std::thread::id();  // empty id
        } else if constexpr (mode == LockMode::SHARED) {
            lock.shared_owners.erase(std::this_thread::get_id());
        }
    }

    /* Swap state with another RecursiveThreadGuard. */
    inline void swap(RecursiveThreadGuard& other) {
        guard.swap(other.guard);
    }

    /* Check if the guard owns a lock on the associated mutex. */
    inline bool active() const noexcept {
        return guard.has_value() && guard.value().active();
    }

    /* Check if the guard owns a lock on the associated mutex. */
    inline explicit operator bool() const noexcept {
        return active();
    }

};


//////////////////////
////    TRAITS    ////
//////////////////////


/* A collection of traits for introspecting custom lock guards at compile time. */
template <typename GuardType>
class ThreadGuardTraits {
    static_assert(
        std::is_base_of_v<ThreadGuardTag, GuardType>,
        "GuardType not recognized.  Did you forget to inherit from ThreadGuardTag or "
        "specialize ThreadGuardTraits for this type?"
    );

public:
    using Mutex = typename GuardType::mutex_type;

    static constexpr LockMode mode = GuardType::mode;
    static constexpr bool exclusive = (mode == LockMode::EXCLUSIVE);
    static constexpr bool shared = (mode == LockMode::SHARED);
    static constexpr bool recursive = GuardType::recursive;
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


/* Empty type inherited by every custom lock functor.  This makes for easy assertions
during SFINAE trait lookups. */
class LockTag {};


/* A lock functor that uses a simple mutex and a `std::unique_lock` guard. */
class BasicLock : LockTag {
public:
    using Mutex = std::mutex;
    using ExclusiveGuard = ThreadGuard<Mutex, LockMode::EXCLUSIVE>;
    static constexpr bool recursive = false;
    static constexpr bool spin = false;
    static constexpr bool diagnostic = false;

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
class ReadWriteLock : LockTag {
public:
    using Mutex = std::shared_mutex;
    using ExclusiveGuard = ThreadGuard<Mutex, LockMode::EXCLUSIVE>;
    using SharedGuard = ThreadGuard<Mutex, LockMode::SHARED>;
    static constexpr bool recursive = false;
    static constexpr bool spin = false;
    static constexpr bool diagnostic = false;

    /* Return a std::unique_lock for the internal mutex using RAII semantics.  The
    mutex is automatically acquired when the guard is constructed and released when it
    goes out of scope.  Any operations in between are guaranteed to be atomic.*/
    inline ExclusiveGuard operator()() const noexcept {
        return ExclusiveGuard(mtx);
    }

    /* Return a wrapper around a std::shared_lock for the internal mutex. */
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
 * Lastly, PyGuard is an adapter for a C++ lock guard that allows it to be used as a
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
    using _SharedGuard = typename LockTraits<LockType>::SharedGuard;
    mutable std::unordered_set<std::thread::id> shared_owners;

    template <typename _GuardType, typename _LockType>
    friend class RecursiveGuard;

public:
    using SharedGuard = RecursiveThreadGuard<_SharedGuard, RecursiveLock<LockType>>;

    /* Acquire the lock in shared mode, allowing repeated locks within a single
    thread. */
    template <typename... Args>
    SharedGuard shared(Args&&... args) const {
        auto id = std::this_thread::get_id();

        // if the current thread already owns the lock, return an empty guard
        if (shared_owners.find(id) != shared_owners.end()) {
            return SharedGuard(*this);
        }

        // NOTE: As written, the handling of the `shared_owners` set is atomic since we
        // only modify it AFTER a lock has been acquired.  This guarantees that we will
        // not encounter race conditions during the above membership check.

        _SharedGuard guard = LockType::shared(std::forward<Args>(args)...);  // blocks
        shared_owners.insert(id);
        return SharedGuard(*this, std::move(guard));
    }

};


/* A lock decorator that allows a thread to be locked recursively without
deadlocking. */
template <typename LockType>
class RecursiveLock : public _RecursiveLock<LockType, LockTraits<LockType>::shared> {
    using _ExclusiveGuard = typename LockTraits<LockType>::ExclusiveGuard;
    mutable std::thread::id owner;

    template <typename _GuardType, typename _LockType>
    friend class RecursiveGuard;

public:
    using ExclusiveGuard = RecursiveThreadGuard<_ExclusiveGuard, RecursiveLock>;
    static constexpr bool recursive = true;
    static constexpr bool spin = LockTraits<LockType>::spin;
    static constexpr bool diagnostic = LockTraits<LockType>::diagnostic;

    /* Acquire the lock in exclusive mode, allowing repeated locks within a single
    thread. */
    template <typename... Args>
    ExclusiveGuard operator()(Args&&... args) const {
        auto id = std::this_thread::get_id();

        if (id == owner) {
            return ExclusiveGuard(*this);
        }

        // NOTE: As written, the handling of the `owner` identifier is atomic since
        // we only modify it AFTER a lock has been acquired.  This guarantees that we
        // will not encounter race conditions during the above comparison.

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

        msg << duration.count();

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

        return msg.str();
    }

public:
    static constexpr bool recursive = LockTraits<LockType>::recursive;
    static constexpr bool spin = true;
    static constexpr bool diagnostic = LockTraits<LockType>::diagnostic;
    static constexpr int max_retries = MaxRetries;
    static constexpr TimeoutUnit timeout = TimeoutUnit(TimeoutValue);
    static constexpr WaitUnit wait = WaitUnit(WaitValue);

    /* Acquire an exclusive lock, spinning according to the template parameters. */
    template <typename... Args>
    auto operator()(Args&&... args) const
        -> decltype(LockType::operator()(std::forward<Args>(args)...))
    {
        using Guard = decltype(LockType::operator()(std::forward<Args>(args)...));
        auto end = Clock::now() + timeout;
        int retries = 0;

        // loop until the lock is acquired or we hit an error condition
        while (true) {
            std::optional<Guard> guard = LockType::try_lock();
            if (guard.has_value()) {
                return guard.value();
            }

            if constexpr (max_retries >= 0) {
                if (++retries >= max_retries) {
                    std::ostringstream msg;
                    msg << "failed to acquire exclusive lock (exceeded max retries: ";
                    msg << max_retries << ")";
                    throw std::runtime_error(msg.str());
                }
            }

            if constexpr (TimeoutValue >= 0) {
                if (Clock::now() > end) {
                    std::ostringstream msg;
                    msg << "failed to acquire exclusive lock (exceeded timeout: ";
                    msg << duration_to_string(timeout) << ")";
                    throw std::runtime_error(msg.str());
                }
            }

            if constexpr (WaitValue > 0) {
                std::this_thread::sleep_for(wait);
            }
        }
        // indefinite loop
    }

    /* Acquire a shared lock, spinning according to the template parameters. */
    template <typename... Args>
    auto shared(Args&&... args) const
        -> decltype(LockType::shared(std::forward<Args>(args)...))
    {
        using Guard = decltype(LockType::shared(std::forward<Args>(args)...));
        auto end = Clock::now() + timeout;
        int retries = 0;

        // loop until the lock is acquired or we hit an error condition
        while (true) {
            std::optional<Guard> guard = LockType::try_shared();
            if (guard.has_value()) {
                return guard.value();
            }

            if constexpr (max_retries > 0) {
                if (++retries >= max_retries) {
                    std::ostringstream msg;
                    msg << "failed to acquire shared lock (exceeded max retries: ";
                    msg << max_retries << ")";
                    throw std::runtime_error(msg.str());
                }
            }

            if constexpr (TimeoutValue > 0) {
                if (Clock::now() > end) {
                    std::ostringstream msg;
                    msg << "failed to acquire shared lock (exceeded timeout: ";
                    msg << duration_to_string(timeout) << ")";
                    throw std::runtime_error(msg.str());
                }
            }

            if constexpr (WaitValue > 0) {
                std::this_thread::sleep_for(wait);
            }
        }
        // indefinite loop
    }

};


/* A lock decorator that tracks performance diagnostics for the lock. */
template <typename LockType, typename Unit = std::chrono::nanoseconds>
class DiagnosticLock : public LockType {
    using Clock = std::chrono::high_resolution_clock;
    using Resolution = std::chrono::duration<double, typename Unit::period>;
    mutable size_t lock_count = 0;
    mutable Resolution lock_time = {};

public:
    static constexpr bool recursive = LockTraits<LockType>::recursive;
    static constexpr bool spin = LockTraits<LockType>::spin;
    static constexpr bool diagnostic = true;

    /* Track the elapsed time to acquire a lock on the internal mutex. */
    template <typename... Args>
    auto operator()(Args&&... args) const {
        auto start = Clock::now();

        auto result = LockType::operator()(std::forward<Args>(args)...);

        auto end = Clock::now();
        lock_time += std::chrono::duration_cast<Resolution>(end - start);
        ++lock_count;
        return result;
    }

    /* Track the elapsed time to acquire a shared lock, if enabled. */
    template <typename... Args>
    auto shared(Args&&... args) const
        -> decltype(LockType::shared(std::forward<Args>(args)...))
    {
        auto start = Clock::now();

        auto result = LockType::shared(std::forward<Args>(args)...);

        auto end = Clock::now();
        lock_time += std::chrono::duration_cast<Resolution>(end - start);
        ++lock_count;
        return result;
    }

    /* Get the total number of times the mutex has been locked. */
    inline size_t count() const {
        return lock_count;
    }

    /* Get the total time spent waiting to acquire the lock. */
    inline Resolution duration() const {
        return lock_time;
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


//////////////////////
////    TRAITS    ////
//////////////////////


/* A collection of traits for introspecting lock functors at compile time. */
template <typename LockType>
class LockTraits {
    static_assert(
        std::is_base_of_v<LockTag, LockType>,
        "LockType not recognized.  Did you forget to inherit from LockTag or "
        "specialize LockTraits for this type?"
    );

    /* Detects whether the templated lock has an overloaded call operator, indicating
    an exclusive locking strategy. */
    struct _exclusive {
        template <typename T>
        static constexpr auto test(T* t) -> decltype(t->operator()());
        template <typename T>
        static constexpr auto test(...) -> void;
        using type = decltype(test<LockType>(nullptr));
        static constexpr bool value = !std::is_void_v<type>;
    };

    /* Detects whether the templated lock has a shared() method, indicating a
    read/write locking strategy. */
    struct _shared {
        template <typename T>
        static constexpr auto test(T* t) -> decltype(t->shared());
        template <typename T>
        static constexpr auto test(...) -> void;
        using type = decltype(test<LockType>(nullptr));
        static constexpr bool value = !std::is_void_v<type>;
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
        static constexpr auto test(...) {
            return std::chrono::nanoseconds(-1);
        }
        using type = decltype(test<LockType>());
        static constexpr type value = test<LockType>();
    };

    /* Get the unit used for diagnostic durations. */
    struct _diagnostic_unit {
        template <typename T>
        static constexpr auto test(T* t) -> decltype(t->contention());
        template <typename T>
        static constexpr auto test(...) -> void;
        using type = decltype(test<LockType>(nullptr));
    };

public:
    using Mutex = typename LockType::Mutex;
    using ExclusiveGuard = typename _exclusive::type;  // void if not exclusive
    using SharedGuard = typename _shared::type;  // void if not shared
    using TimeoutUnit = typename _timeout::type;  // std::chrono::ns if not spin
    using WaitUnit = typename _wait::type;  // std::chrono::ns if not spin
    using DiagnosticUnit = typename _diagnostic_unit::type;  // void if not diagnostic

    static constexpr bool exclusive = _exclusive::value;
    static constexpr bool shared = _shared::value;
    static constexpr bool recursive = LockType::recursive;
    static constexpr bool spin = LockType::spin;
    static constexpr int max_retries = _max_retries::value;  // -1 if not spin
    static constexpr TimeoutUnit timeout = _timeout::value;  // -1ns if not spin
    static constexpr WaitUnit wait = _wait::value;  // -1ns if not spin
    static constexpr bool diagnostic = LockType::diagnostic;

};


///////////////////////////////
////    PYTHON WRAPPERS    ////
///////////////////////////////


/* A lock decorator that produces Python-compatible lock guards as context managers. */
template <typename LockType>
class PyLock {
    using Traits = LockTraits<LockType>;

    PyObject_HEAD
    LockType* lock;

public:
    PyLock() = delete;
    PyLock(const PyLock&) = delete;
    PyLock(PyLock&&) = delete;
    PyLock& operator=(const PyLock&) = delete;
    PyLock& operator=(PyLock&&) = delete;

    /* A wrapper around a C++ thread guard that allows it to be used as a Python
    context manager. */
    template <typename GuardType>
    class PyThreadGuard {
        using Traits = ThreadGuardTraits<GuardType>;

        PyObject_HEAD
        LockType* lock;
        bool has_guard;
        union {
            GuardType guard;
        };

    public:
        PyThreadGuard() = delete;
        PyThreadGuard(const PyThreadGuard&) = delete;
        PyThreadGuard(PyThreadGuard&&) = delete;
        PyThreadGuard& operator=(const PyThreadGuard&) = delete;
        PyThreadGuard& operator=(PyThreadGuard&&) = delete;

        static PyObject* __enter__(PyThreadGuard* self, PyObject* /* ignored */) {
            if (self->has_guard) {
                PyErr_SetString(
                    PyExc_RuntimeError,
                    "lock has already been acquired by this context manager"
                );
                return nullptr;
            }

            try {
                if constexpr (Traits::mode == LockMode::EXCLUSIVE) {
                    new (&self->guard) GuardType(self->lock->operator()());
                } else if constexpr (Traits::mode == LockMode::SHARED) {
                    new (&self->guard) GuardType(self->lock->shared());
                } else {
                    PyErr_SetString(PyExc_RuntimeError, "unrecognized lock mode");
                    return nullptr;
                }

            } catch (...) {
                throw_python();
                return nullptr;
            }

            self->has_guard = true;
            return Py_NewRef(self);
        }

        inline static PyObject* __exit__(PyThreadGuard* self, PyObject* /* ignored */) {
            if (self->has_guard) {
                self->guard.~GuardType();
                self->has_guard = false;
            }
            Py_RETURN_NONE;
        }

        inline static PyObject* active(PyThreadGuard* self, void* /* ignored */) {
            return PyBool_FromLong(self->has_guard);
        }

        inline static PyObject* is_shared(PyThreadGuard* self, void* /* ignored */) {
            return PyBool_FromLong(Traits::mode == LockMode::SHARED);
        }

    private:
        friend PyLock;

        /* Construct a Python lock from a C++ lock guard. */
        inline static PyObject* construct(LockType* lock) {
            PyThreadGuard* self = PyObject_New(PyThreadGuard, &Type);
            if (self == nullptr) {
                PyErr_SetString(
                    PyExc_RuntimeError,
                    "failed to allocate memory for PyLock guard"
                );
                return nullptr;
            }

            self->lock = lock;
            self->has_guard = false;
            return reinterpret_cast<PyObject*>(self);
        }

        /* Release the lock when the context manager is garbage collected, if it hasn't
        been released already. */
        inline static void __dealloc__(PyThreadGuard* self) {
            if (self->has_guard) {
                self->guard.~GuardType();
                self->has_guard = false;
            }
            Type.tp_free(self);
        }

        struct docs {

            static constexpr std::string_view PyThreadGuard {R"doc(
A Python-compatible wrapper around a C++ ThreadGuard that allows it to be used
as a context manager.

Notes
-----
This class is only meant to be instantiated via the ``lock`` functor of a
custom data structure.  It is directly equivalent to constructing a C++
RAII-style lock guard (e.g. a ``std::unique_lock`` or similar) within the
guarded context.  The C++ guard is automatically destroyed upon exiting the
context.
)doc"
            };

            static constexpr std::string_view __enter__ {R"doc(
Enter the context manager's context block, acquiring a lock on the mutex.

Returns
-------
PyThreadGuard
    The context manager itself, which may be aliased using the `as` keyword.
)doc"
            };

            static constexpr std::string_view __exit__ {R"doc(
Exit the context manager's context block, releasing the lock.
)doc"
            };

            static constexpr std::string_view active {R"doc(
Check if the lock is acquired within the current context.

Returns
-------
bool
    True if the lock is currently active, False otherwise.
)doc"
            };

            static constexpr std::string_view is_shared {R"doc(
Check if the lock allows for concurrent reads.

Returns
-------
bool
    True if the lock is shared, False otherwise.
)doc"
            };

        };

        inline static PyGetSetDef properties[] = {
            {"active", (getter) active, NULL, docs::active.data()},
            {"is_shared", (getter) is_shared, NULL, docs::is_shared.data()},
            {NULL}  // sentinel
        };

        inline static PyMethodDef methods[] = {
            {"__enter__", (PyCFunction) __enter__, METH_NOARGS, docs::__enter__.data()},
            {"__exit__", (PyCFunction) __exit__, METH_VARARGS, docs::__exit__.data()},
            {NULL}  // sentinel
        };

        static PyTypeObject build_type() {
            PyTypeObject slots = {
                .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
                .tp_name = PyName<GuardType>.data(),
                .tp_basicsize = sizeof(PyThreadGuard),
                .tp_dealloc = (destructor) __dealloc__,
                .tp_flags = (
                    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_IMMUTABLETYPE |
                    Py_TPFLAGS_DISALLOW_INSTANTIATION
                ),
                .tp_doc = docs::PyThreadGuard.data(),
                .tp_methods = methods,
                .tp_getset = properties,
            };

            if (PyType_Ready(&slots) < 0) {
                throw std::runtime_error("could not initialize PyThreadGuard type");
            }
            return slots;
        }

    public:

        /* Final Python type. */
        inline static PyTypeObject Type = build_type();
    };

    /* Construct a Python wrapper around a C++ lock functor. */
    inline static PyObject* construct(LockType& lock) {
        PyLock* result = PyObject_New(PyLock, &Type);
        if (result == nullptr) {
            PyErr_SetString(PyExc_RuntimeError, "could not allocate Python lock proxy");
            return nullptr;
        }

        result->lock = &lock;
        return reinterpret_cast<PyObject*>(result);
    }

    /* Wrap an exclusive lock as a Python context manager. */
    inline static PyObject* __call__(PyLock* self, PyObject* args, PyObject* kwargs) {
        static constexpr std::string_view meth_name{"lock"};

        try {
            PyArgs<CallProtocol::KWARGS> pyargs(meth_name, args, kwargs);
            pyargs.finalize();  // no args allowed

            if constexpr (!Traits::exclusive) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "lock cannot be acquired in exclusive mode"
                );
                return nullptr;
            } else {
                using Guard = typename LockTraits<LockType>::ExclusiveGuard;
                return PyThreadGuard<Guard>::construct(self->lock);
            }

        } catch (...) {
            throw_python();
            return nullptr;
        }
    }

    /* Wrap a shared lock as a Python context manager. */
    inline static PyObject* shared(PyLock* self, PyObject* /* ignored */) {
        if constexpr (!Traits::shared) {
            PyErr_SetString(
                PyExc_TypeError,
                "lock cannot be acquired in shared mode"
            );
            return nullptr;
        } else {
            using Guard = typename LockTraits<LockType>::SharedGuard;
            return PyThreadGuard<Guard>::construct(self->lock);
        }
    }

    /* Get the total number of times the mutex has been locked. */
    inline static PyObject* count(PyLock* self, void* /* ignored */) {
        if constexpr (!Traits::diagnostic) {
            PyErr_SetString(PyExc_TypeError, "lock does not track diagnostics");
            return nullptr;
        } else {
            return PyLong_FromSize_t(self->lock->count());
        }
    }

    // TODO: duration should maybe accept a unit as a string.  Internally, we always
    // store with nanosecond duration, but we can convert to other units for display
    // purposes.  The duration() method always returns a double.  We offer a second
    // method ns() that returns the nanosecond duration as an integer.

    /* Get the total time spent waiting to acquire the lock. */
    inline static PyObject* duration(PyLock* self, void* /* ignored */) {
        if constexpr (!Traits::diagnostic) {
            PyErr_SetString(PyExc_TypeError, "lock does not track diagnostics");
            return nullptr;
        } else {
            return PyFloat_FromDouble(self->lock->duration().count());
        }
    }

    // TODO: if duration accepts an optional unit, then contention should too.

    /* Get the average time spent waiting to acquire the lock. */
    inline static PyObject* contention(PyLock* self, void* /* ignored */) {
        if constexpr (!Traits::diagnostic) {
            PyErr_SetString(PyExc_TypeError, "lock does not track diagnostics");
            return nullptr;
        } else {
            return PyFloat_FromDouble(self->lock->contention().count());
        }
    }

    /* Reset the internal diagnostic counters. */
    inline static PyObject* reset_diagnostics(PyLock* self, PyObject* /* ignored */) {
        if constexpr (!Traits::diagnostic) {
            PyErr_SetString(PyExc_TypeError, "lock does not track diagnostics");
            return nullptr;
        } else {
            self->lock->reset_diagnostics();
            Py_RETURN_NONE;
        }
    }

private:

    /* Deallocate the lock functor. */
    inline static void __dealloc__(PyLock* self) {
        Type.tp_free(self);
    }

    struct docs {

        static constexpr std::string_view shared {R"doc(
Acquire a shared lock on the mutex.

Returns
-------
context manager
    A context manager that acquires the lock upon entering its context and releases it
    upon exiting.

Raises
------
TypeError
    If the lock cannot be acquired in shared mode.

Notes
-----
This method is only available if the underlying lock functor supports shared locks.
Otherwise, it will raise an error.

Examples
--------
.. doctest::

    >>> from bertrand.structs import LinkedList
    >>> l = LinkedList("abc", lock={"shared": True})
    >>> with l.lock.shared() as guard:  # blocks until lock is acquired
    ...     # list may be accessed concurrently as long as no exclusive locks are held
    ...     print(guard.locked)
    ... print(guard.locked)
    True
    False
)doc"
        };

        static constexpr std::string_view count {R"doc(
Get the total number of times the mutex has been locked.

Returns
-------
int
    The total number of times the mutex has been locked in either shared or exclusive
    mode.

Raises
------
TypeError
    If the lock does not track diagnostics.

Examples
--------
.. doctest::

    >>> from bertrand.structs import LinkedList
    >>> l = LinkedList("abc", lock={"diagnostic": True})
    >>> l.lock.count
    0
    >>> with l.lock() as guard:
    ...     pass
    >>> l.lock.count
    1
)doc"
        };

        static constexpr std::string_view duration {R"doc(
Get the total time spent waiting to acquire the lock.

Parameters
----------
unit : str, default "ns"
    The unit to use for the returned duration.  Must be one of "ns", "us", "ms", "s",
    "m", or "h".

Returns
-------
float
    The total time spent waiting to acquire the lock, in the specified units.

Raises
------
TypeError
    If the lock does not track diagnostics.

Examples
--------
# TODO: add example showing contention between multiple threads
)doc"
        };

        static constexpr std::string_view contention {R"doc(
Get the average time spent waiting to acquire the lock.

Parameters
----------
unit : str, default "ns"
    The unit to use for the returned duration.  Must be one of "ns", "us", "ms", "s",
    "m", or "h".

Returns
-------
float
    The average time spent waiting to acquire the lock, in the specified units.

Raises
------
TypeError
    If the lock does not track diagnostics.

Examples
--------
# TODO: add example showing contention between multiple threads
)doc"
        };

        static constexpr std::string_view reset_diagnostics {R"doc(
Reset the internal diagnostic counters.

Raises
------
TypeError
    If the lock does not track diagnostics.

Examples
--------
.. doctest::

    >>> from bertrand.structs import LinkedList
    >>> l = LinkedList("abc", lock={"diagnostic": True})
    >>> with l.lock() as guard:
    ...     pass
    >>> l.lock.count
    1
    >>> l.lock.reset_diagnostics()
    >>> l.lock.count
    0
)doc"
        };

    };

    inline static PyGetSetDef properties[] = {
        {"count", (getter) count, NULL, docs::count.data(), NULL},
        {"duration", (getter) duration, NULL, docs::duration.data(), NULL},
        {"contention", (getter) contention, NULL, docs::contention.data(), NULL},
        {NULL}  // sentinel
    };

    inline static PyMethodDef methods[] = {
        {"shared", (PyCFunction) shared, METH_NOARGS, docs::shared.data()},
        {"reset_diagnostics", (PyCFunction) reset_diagnostics, METH_NOARGS, docs::reset_diagnostics.data()},
        {NULL}  // sentinel
    };

    static PyTypeObject build_type() {
        PyTypeObject slots = {
            .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
            .tp_name = PyName<LockType>.data(),
            .tp_basicsize = sizeof(PyLock),
            .tp_dealloc = (destructor) __dealloc__,
            .tp_call = (ternaryfunc) __call__,
            .tp_flags = (
                Py_TPFLAGS_DEFAULT | Py_TPFLAGS_IMMUTABLETYPE |
                Py_TPFLAGS_DISALLOW_INSTANTIATION
            ),
            .tp_doc = "Python-compatible wrapper around a C++ lock functor.",
            .tp_methods = methods,
            .tp_getset = properties,
        };

        if (PyType_Ready(&slots) < 0) {
            throw std::runtime_error("could not initialize PyLock type");
        }
        return slots;
    }

public:

    inline static PyTypeObject Type = build_type();

};


}  // namespace util


/* Export to base namespace. */
using util::BasicLock;
using util::ReadWriteLock;
using util::RecursiveLock;
using util::SpinLock;
using util::DiagnosticLock;


}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_UTIL_THREAD_H
