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
template <typename View, typename Lock, const std::string_view& name>
class LinkedBase {
public:
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
        // NOTE: the new capacity is absolute, not relative to the current capacity.
        // If a capacity of 25 is requested, then the allocator array will be resized
        // to house at least 25 nodes, regardless of the current capacity.
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

    /* Forward the view.iter() functor. */
    class IteratorFactory {
    public:
        /* A wrapper around a view iterator that yields values rather than nodes. */
        template <linked::Direction dir>
        class Iterator {
            using Wrapped = typename View::template Iterator<dir>;

        public:
            /* iterator tags for std::iterator_traits */
            using iterator_category     = typename Wrapped::iterator_category;
            using difference_type       = typename Wrapped::difference_type;
            using value_type            = typename Wrapped::Node::Value;
            using pointer               = typename Wrapped::Node::Value*;
            using reference             = typename Wrapped::Node::Value&;

            /* Dereference the iterator to get the value at the current node. */
            inline PyObject* operator*() const {
                return (*iter)->value();
            }

            /* Advance the iterator to the next node. */
            inline Iterator& operator++() {
                ++iter;
                return *this;
            }

            /* Inequality comparison to terminate the loop. */
            inline bool operator!=(const Iterator& other) const {
                return iter != other.iter;
            }

        private:
            friend IteratorFactory;
            Wrapped iter;

            Iterator(Wrapped&& iter) : iter(std::move(iter)) {}
        };

        /* Invoke the functor to get a coupled iterator over the list. */
        inline auto operator()() const {
            return util::CoupledIterator<Iterator<linked::Direction::forward>>(
                parent.iter()
            );
        }

        /* Get a coupled reverse iterator over the list. */
        inline auto reverse() const {
            return util::CoupledIterator<Iterator<linked::Direction::backward>>(
                parent.iter.reverse()
            );
        }

        /* Get a forward iterator to the head of the list. */
        inline auto begin() const {
            return Iterator<linked::Direction::forward>(parent.view.begin());
        }

        /* Get an empty forward iterator to terminate the sequence. */
        inline auto end() const {
            return Iterator<linked::Direction::forward>(parent.view.end());
        }

        /* Get a backward iterator to the tail of the list. */
        inline auto rbegin() const {
            return Iterator<linked::Direction::backward>(parent.view.rbegin());
        }

        /* Get an empty backward iterator to terminate the sequence. */
        inline auto rend() const {
            return Iterator<linked::Direction::backward>(parent.view.rend());
        }

        /* Get a forward Python iterator over the list. */
        inline PyObject* python() const {
            static constexpr std::string_view suffix = ".iter";
            using Iter = util::PyIterator<
                Iterator<linked::Direction::forward>, util::string::concat<name, suffix>
            >;

            // create Python iterator over list
            return Iter::init(begin(), end());
        }

        /* Get a reverse Python iterator over the list. */
        inline PyObject* rpython() const {
            static constexpr std::string_view suffix = ".reverse_iter";
            using Iter = util::PyIterator<
                Iterator<linked::Direction::backward>, util::string::concat<name, suffix>
            >;

            // create Python iterator over list
            return Iter::init(rbegin(), rend());
        }

    private:
        friend LinkedBase;
        LinkedBase<View, Lock, name>& parent;

        IteratorFactory(LinkedBase<View, Lock, name>& parent) : parent(parent) {}
    };

    /* Functor to create various kinds of iterators over the list. */
    const IteratorFactory iter;

    /* Get a forward iterator to the start of the list. */
    inline auto begin() const {
        return iter.begin();
    }

    /* Get a forward iterator to the end of the list. */
    inline auto end() const {
        return iter.end();
    }

    /* Get a reverse iterator to the end of the list. */
    inline auto rbegin() const {
        return iter.rbegin();
    }

    /* Get a reverse iterator to the start of the list. */
    inline auto rend() const {
        return iter.rend();
    }

    ///////////////////////////////
    ////    THREADING LOCKS    ////
    ///////////////////////////////

    /* Adapt lock policy to allow for Python context managers as locks. */
    class LockFactory : public Lock {
    public:

        /* Wrap a lock guard as a Python context manager. */
        template <typename... Args>
        inline PyObject* python(Args&&... args) const {
            static constexpr std::string_view suffix = ".lock";
            using namespace util;
            return PyLock<LockFactory, string::concat<name, suffix>>::init(this);
        }

        // TODO: shared_python() is exactly the same as python().

        /* Wrap a shared lock as a Python context manager. */
        template <bool is_shared = Lock::is_shared, typename... Args>
        inline auto shared_python(Args&&... args) const
            -> std::enable_if_t<is_shared, PyObject*>
        {
            static constexpr std::string_view suffix = ".shared_lock";
            using namespace util;
            return PyLock<LockFactory, string::concat<name, suffix>>::init(this);
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
    ) : view(max_size, spec), iter(*this)
    {}

    /* Construct a list from an input iterable. */
    LinkedBase(
        PyObject* iterable,
        bool reverse = false,
        std::optional<size_t> max_size = std::nullopt,
        PyObject* spec = nullptr
    ) : view(iterable, reverse, max_size, spec), iter(*this)
    {}

    /* Construct a list from a base view. */
    LinkedBase(View&& view) : view(std::move(view)), iter(*this) {}

    // TODO: construct from iterators?

    /* Copy constructor. */
    LinkedBase(const LinkedBase& other) :
        view(other.view), iter(*this)
    {}

    /* Move constructor. */
    LinkedBase(LinkedBase&& other) :
        view(std::move(other.view)), iter(*this)
    {}

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
