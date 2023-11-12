// include guard: BERTRAND_STRUCTS_LINKED_BASE_H
#ifndef BERTRAND_STRUCTS_LINKED_BASE_H
#define BERTRAND_STRUCTS_LINKED_BASE_H

#include <cstddef>      // size_t
#include <memory>  // std::shared_ptr, std::weak_ptr
#include <optional>     // std::optional
#include <stdexcept>    // std::runtime_error
#include <string_view>  // std::string_view
#include <variant>  // std::visit
#include "core/iter.h"  // Direction
#include "../util/iter.h"  // iter(), IterProxy
#include "../util/python.h"  // PyIterator
#include "../util/string.h"  // string concatenation
#include "../util/thread.h"  // Lock, PyLock


namespace bertrand {
namespace structs {
namespace linked {


////////////////////
////    BASE    ////
////////////////////


/* Empty tag class marking a linked data structure.

Using an empty class like this allows for easy SFINAE checks via a simple
std::is_base_of check, without requiring any foreknowledge of template parameters. */
class LinkedTag {};


/* Base class that forwards the public members of the underlying view. */
template <typename ViewType, typename LockType>
class LinkedBase : public LinkedTag {
    using Direction = linked::Direction;

public:
    using View = ViewType;
    using Node = typename View::Node;
    using Value = typename View::Value;
    using MemGuard = typename View::MemGuard;

    template <Direction dir>
    using Iterator = typename View::template Iterator<dir>;
    template <Direction dir>
    using ConstIterator = typename View::template ConstIterator<dir>;

    /* Every LinkedList contains a view that manages low-level node
    allocation/deallocation and links between nodes. */
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
    LinkedBase(
        PyObject* iterable,
        std::optional<size_t> max_size = std::nullopt,
        PyObject* spec = nullptr,
        bool reverse = false
    ) : view(iterable, max_size, spec, reverse)
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

    /* Check whether the allocator supports dynamic resizing. */
    inline bool dynamic() const noexcept {
        return view.dynamic();
    }

    /* Check whether the allocator is currently frozen for memory stability. */
    inline bool frozen() const noexcept {
        return view.frozen();
    }

    /* Reserve memory for a specific number of nodes ahead of time. */
    inline MemGuard reserve(std::optional<size_t> capacity = std::nullopt) {
        // NOTE: the new capacity is absolute, not relative to the current capacity.  If
        // a capacity of 25 is requested (for example), then the allocator array will be
        // resized to house at least 25 nodes, regardless of the current capacity.
        return view.reserve(capacity);
    }

    /* Rearrange the allocator array to reflect the current list order. */
    inline void defragment() {
        view.defragment();
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
        return sizeof(LinkedBase) + view.nbytes();
    }

    /////////////////////////////////
    ////    ITERATOR PROTOCOL    ////
    /////////////////////////////////

    inline Iterator<Direction::forward> begin() { return view.begin(); }
    inline Iterator<Direction::forward> end() { return view.end(); }
    inline Iterator<Direction::backward> rbegin() { return view.rbegin(); }
    inline Iterator<Direction::backward> rend() { return view.rend(); }
    inline ConstIterator<Direction::forward> begin() const { return view.begin(); }
    inline ConstIterator<Direction::forward> end() const { return view.end(); }
    inline ConstIterator<Direction::backward> rbegin() const { return view.rbegin(); }
    inline ConstIterator<Direction::backward> rend() const { return view.rend(); }
    inline ConstIterator<Direction::forward> cbegin() const { return view.cbegin(); }
    inline ConstIterator<Direction::forward> cend() const { return view.cend(); }
    inline ConstIterator<Direction::backward> crbegin() const { return view.crbegin(); }
    inline ConstIterator<Direction::backward> crend() const { return view.crend(); }

    ///////////////////////////////
    ////    THREADING LOCKS    ////
    ///////////////////////////////

    /* Functor that produces threading locks for a linked data structure. */
    util::PyLock<LockType> lock;
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

};


//////////////////////////////
////    CYTHON HELPERS    ////
//////////////////////////////


namespace cython {


/* A functor that generates weak references for a type-erased Cython variant. */
template <typename T>
class SelfRef {
public:

    /* A weak reference to the associated object. */
    class WeakRef {
    public:

        /* Check whether the referenced object still exists. */
        bool exists() const {
            return !ref.expired();
        }

        /* Follow the weak reference, yielding a pointer to the referenced object if it
        still exists.  Otherwise, sets a Python error and return nullptr.  */
        T* get() const {
            if (ref.expired()) {
                throw std::runtime_error("referenced object no longer exists");
            }
            return ref.lock().get();
        }

    private:
        friend SelfRef;
        std::weak_ptr<T> ref;

        template <typename... Args>
        WeakRef(Args... args) : ref(std::forward<Args>(args)...) {}
    };

    /* Get a weak reference to the associated object. */
    WeakRef operator()() const {
        return WeakRef(_self);
    }

private:
    friend T;
    const std::shared_ptr<T> _self;

    // NOTE: custom deleter prevents the shared_ptr from trying to delete the object
    // when it goes out of scope, which can cause a segfault due to a double free.

    SelfRef(T& self) : _self(&self, [](auto&) {}) {}
};


/* A functor that allows a type-erased Cython variant to be locked for use in a
multithreaded environment. */
template <typename T>
class VariantLock {
public:

    /* Return a Python context manager containing an exclusive lock on the mutex. */
    inline PyObject* operator()() const {
        return std::visit(
            [&](auto& obj) {
                return obj.lock.python();
            }, 
            ref.variant
        );
    }

    /* Return a Python context manager containing a shared lock on the mutex. */
    inline PyObject* shared() const {
        return std::visit(
            [&](auto& obj) {
                return obj.lock.shared_python();
            }, 
            ref.variant
        );
    }

    /* Get the total number of times the mutex has been locked. */
    inline size_t count() const {
        return std::visit(
            [&](auto& obj) {
                return obj.lock.count();
            },
            ref.variant
        );
    }

    /* Get the total length of time spent waiting to acquire the lock. */
    inline size_t duration() const {
        return std::visit(
            [&](auto& obj) {
                return obj.lock.duration();
            },
            ref.variant
        );
    }

    /* Get the average time spent waiting to acquire the lock. */
    inline double contention() const {
        return std::visit(
            [&](auto& obj) {
                return obj.lock.average();
            },
            ref.variant
        );
    }

    /* Reset the internal diagnostic counters. */
    inline void reset_diagnostics() const {
        std::visit(
            [&](auto& obj) {
                obj.lock.reset_diagnostics();
            },
            ref.variant
        );
    }

private:
    friend T;
    T& ref;

    VariantLock(T& variant) : ref(variant) {}
};


}  // namespace cython


//////////////////////////////
////    PYTHON WRAPPER    ////
//////////////////////////////


// TODO: SelfRef is only needed for RelativeProxies, which are not yet implemented.

// TODO: VariantLock should be an inner class within PyLinkedBase, and should be
// exposed as a PyObject* member attribute.


/* A CRTP-enabled base class that exposes properties inherited from LinkedBase to
Python. */
template <typename Derived>
class PyLinkedBase {
public:

    /* Getter for `LinkedList.capacity` in Python. */
    inline static PyObject* capacity(Derived* self, PyObject* /* ignored */) noexcept {
        size_t result = std::visit(
            [](auto& list) {
                return list.capacity();
            },
            self->variant
        );
        return PyLong_FromSize_t(result);
    }

    /* Getter for `LinkedList.max_size` in Python. */
    inline static PyObject* max_size(Derived* self, PyObject* /* ignored */) noexcept {
        std::optional<size_t> result = std::visit(
            [](auto& list) {
                return list.max_size();
            },
            self->variant
        );
        if (result.has_value()) {
            return PyLong_FromSize_t(result.value());
        } else {
            Py_RETURN_NONE;
        }
    }

    /* Getter for `LinkedList.dynamic` in Python. */
    inline static PyObject* dynamic(Derived* self, PyObject* /* ignored */) noexcept {
        bool result = std::visit(
            [](auto& list) {
                return list.dynamic();
            },
            self->variant
        );
        return PyBool_FromLong(result);
    }

    /* Getter for `LinkedList.frozen` in Python. */
    inline static PyObject* frozen(Derived* self, PyObject* /* ignored */) noexcept {
        bool result = std::visit(
            [](auto& list) {
                return list.frozen();
            },
            self->variant
        );
        return PyBool_FromLong(result);
    }

    /* Getter for `LinkedList.nbytes` in Python. */
    inline static PyObject* nbytes(Derived* self, PyObject* /* ignored */) noexcept {
        size_t result = std::visit(
            [](auto& list) {
                return list.nbytes();
            },
            self->variant
        );
        return PyLong_FromSize_t(result);
    }

    /* Getter for `LinkedList.specialization` in Python. */
    inline static PyObject* specialization(Derived* self, PyObject* /* ignored */) noexcept {
        PyObject* result = std::visit(
            [](auto& list) {
                return list.specialization();
            },
            self->variant
        );
        return Py_XNewRef(result);
    }

    // TODO: For reserve() to be available at the Python level, we need to create a
    // Python wrapper like we did with threading locks/iterators.

    // /* Implement `LinkedList.reserve()` in Python. */
    // inline PyObject* reserve(size_t size) {
    //     std::visit(
    //         [&size](auto& list) {
    //             return list.reserve(size);
    //         },
    //         this->variant
    //     );
    // }

    /* Implement `LinkedList.defragment()` in Python. */
    inline static PyObject* defragment(Derived* self, PyObject* /* ignored */) {
        std::visit(
            [](auto& list) {
                list.defragment();
            },
            self->variant
        );
        Py_RETURN_NONE;  // void
    }

    /* Implement `LinkedList.specialize()` in Python. */
    inline static PyObject* specialize(Derived* self, PyObject* spec) {
        std::visit(
            [&spec](auto& list) {
                list.specialize(spec);
            },
            self->variant
        );
        Py_RETURN_NONE;  // void
    }

    // /* Implement `LinkedList.__class_getitem__()` in Python. */
    // inline PyObject* __class_getitem__() {
    //     // TODO: Create a new heap type for the specialization
    // }

    /* Implement `LinkedList.__len__()` in Python. */
    inline static Py_ssize_t __len__(Derived* self) noexcept {
        return std::visit(
            [](auto& list) {
                return list.size();
            },
            self->variant
        );
    }

    /* Implement `LinkedList.__iter__()` in Python. */
    inline static PyObject* __iter__(Derived* self) noexcept {
        return std::visit(
            [](auto& list) {
                return util::iter(list).python();
            },
            self->variant
        );
    }

    /* Implement `LinkedList.__reversed__()` in Python. */
    inline static PyObject* __reversed__(Derived* self, PyObject* /* ignored */) noexcept {
        return std::visit(
            [](auto& list) {
                return util::iter(list).rpython();
            },
            self->variant
        );
    }

protected:
    const cython::VariantLock<PyLinkedBase> lock;
    PyLinkedBase() : lock(*this) {}

    /* Allocate a new LinkedList instance from Python and register it with the cyclic
    garbage collector. */
    inline static PyObject* __new__(
        PyTypeObject* type,
        PyObject* /* ignored */,
        PyObject* /* ignored */
    ) {
        Derived* self = reinterpret_cast<Derived*>(type->tp_alloc(type, 0));
        if (self == nullptr) return nullptr;
        PyObject_GC_Track(self);  // register with cyclic garbage collector
        return reinterpret_cast<PyObject*>(self);
    }

    /* Deallocate the LinkedList when its Python reference count falls to zero. */
    inline static void __dealloc__(Derived* self) {
        PyObject_GC_UnTrack(self);  // unregister from cyclic garbage collector
        self->~Derived();  // hook into C++ destructor
        Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
    }

    /* Traversal function for Python's cyclic garbage collector. */
    inline static int __traverse__(Derived* self, visitproc visit, void* arg) {
        return std::visit(
            [&](auto& list) {
                for (auto item : list) Py_VISIT(item);
                return 0;
            },
            self->variant
        );
    }

    /* Clear function for Python's cyclic garbage collector. */
    inline static int __clear__(Derived* self) {
        return std::visit(
            [&](auto& list) {
                for (auto item : list) Py_CLEAR(item);
                return 0;
            },
            self->variant
        );
    }

};


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif  // BERTRAND_STRUCTS_LINKED_BASE_H
