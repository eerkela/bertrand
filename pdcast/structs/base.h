// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_BASE_H
#define BERTRAND_STRUCTS_BASE_H

#include <cstddef>      // size_t
#include <optional>     // std::optional
#include <stdexcept>    // std::runtime_error
#include <string_view>  // std::string_view
#include "linked/iter.h"  // Direction
#include "util/coupled_iter.h"  // CoupledIterator
#include "util/python.h"  // PyIterator
#include "util/string.h"  // string concatenation
#include "util/thread.h"  // Lock, PyLock


namespace bertrand {
namespace structs {


/* Base class that forwards the public members of the underlying view. */
template <typename ViewType, typename LockType, const std::string_view& name>
class LinkedBase {
public:
    using View = ViewType;
    using Node = typename View::Node;
    using Value = typename View::Value;

    template <linked::Direction dir>
    using Iterator = typename View::template Iterator<dir>;

    /* Every LinkedList contains a view that manages low-level node
    allocation/deallocation and links between nodes. */
    View view;

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

    /* Reserve memory for a specific number of nodes ahead of time. */
    inline void reserve(size_t capacity) {
        // NOTE: the new capacity is absolute, not relative to the current capacity.  If
        // a capacity of 25 is requested (for example), then the allocator array will be
        // resized to house at least 25 nodes, regardless of the current capacity.
        view.reserve(capacity);
    }

    /* Rearrange the allocator array to reflect the current list order. */
    inline void consolidate() {
        view.consolidate();
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
        return view.nbytes();
    }

    /////////////////////////////////
    ////    ITERATOR PROTOCOL    ////
    /////////////////////////////////

    /* Get a forward iterator to the start of the list. */
    inline Iterator<linked::Direction::forward> begin() const {
        return view.begin();
    }

    /* Get a forward iterator to the end of the list. */
    inline Iterator<linked::Direction::forward> end() const {
        return view.end();
    }

    /* Get a reverse iterator to the end of the list. */
    inline Iterator<linked::Direction::backward> rbegin() const {
        return view.rbegin();
    }

    /* Get a reverse iterator to the start of the list. */
    inline Iterator<linked::Direction::backward> rend() const {
        return view.rend();
    }

    ///////////////////////////////
    ////    THREADING LOCKS    ////
    ///////////////////////////////

    /* Adapt lock policy to allow for Python context managers as locks. */
    class LockFactory : public LockType {
    public:

        /* Wrap a lock guard as a Python context manager. */
        template <typename... Args>
        inline PyObject* python(Args&&... args) const {
            // static constexpr std::string_view suffix = ".lock";
            using namespace util;
            // return PyLock<LockFactory, path::concat_v<name, suffix>>::init(this);
            return PyLock<LockFactory>::init(this);
        }

        // TODO: shared_python() is exactly the same as python().

        /* Wrap a shared lock as a Python context manager. */
        template <bool is_shared = LockType::is_shared, typename... Args>
        inline auto shared_python(Args&&... args) const
            -> std::enable_if_t<is_shared, PyObject*>
        {
            // static constexpr std::string_view suffix = ".shared_lock";
            using namespace util;
            // return PyLock<LockFactory, path::concat_v<name, suffix>>::init(this);
            return PyLock<LockFactory>::init(this);
        }

    };

    /* Functor that produces threading locks for a linked data structure. */
    LockFactory lock;
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

protected:

    /* Construct an empty list. */
    LinkedBase(
        std::optional<size_t> max_size = std::nullopt,
        PyObject* spec = nullptr
    ) :
        view(max_size, spec)
    {}

    /* Construct a list from an input iterable. */
    LinkedBase(
        PyObject* iterable,
        bool reverse = false,
        std::optional<size_t> max_size = std::nullopt,
        PyObject* spec = nullptr
    ) :
        view(iterable, reverse, max_size, spec)
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

};


}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_BASE_H
