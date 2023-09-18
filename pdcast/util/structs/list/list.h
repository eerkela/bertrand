// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_LIST_LIST_H
#define BERTRAND_STRUCTS_LIST_LIST_H

// TODO: additional includes as necessary

#include "core/view.h"
#include "core/sort.h"

#include <sstream>  // std::ostringstream
#include <typeinfo>  // std::bad_typeid


// TODO: should migrate over to using C++ exceptions instead of Python exceptions
// or std::optional.  This will be more consistent with constructor/operator errors.


// TODO: factor out mixins into separate files

/*
structs/
    core/
        allocate.h
        iter.h
        node.h
        view.h
    interface/
        ilist.h
        iset.h
        idict.h
        index.h
        lock.h
        slice.h
        sort.h
    ops/
        listops.h
        setops.h
        dictops.h
    list.h (LinkedList, VariantList, etc.)
    list.pyx
    list.pxd
    list.pyi
*/


////////////////////////////
////    BASE CLASSES    ////
////////////////////////////


/* Base class that forwards the public members of the underlying view. */
template <typename View>
class LinkedBase {
public:
    inline static constexpr bool doubly_linked = View::doubly_linked;

    /* Every LinkedList contains a view that manages low-level node
    allocation/deallocation and links between nodes. */
    View view;

    /* Check if the list contains any elements. */
    inline bool empty() const {
        return view.size == 0;
    }

    /* Get the current size of the list. */
    inline size_t size() const {
        return view.size;
    }

    /* Get the maximum size of the list. */
    inline long long max_size() const {
        return view.max_size;
    }

    /* Get the current specialization for elements of this list. */
    inline PyObject* specialization() const {
        // TODO: reference counting?
        return view.specialization;
    }

    /* Enforce strict type checking for elements of the list. */
    inline void specialize(PyObject* spec) {
        view.specialize(spec);
    }

    /* Get the total amount of memory consumed by the list. */
    inline size_t nbytes() const {
        return view.nbytes();
    }

    /////////////////////////////////
    ////    ITERATOR PROTOCOL    ////
    /////////////////////////////////

    /* Forward the view.iter() functor. */
    class IteratorFactory {
    public:

        template <Direction dir>
        class Iterator;

        template <Direction dir>
        class IteratorPair;

        /* Invoke the functor to get a coupled iterator over the list. */
        inline auto operator()() const {
            return IteratorPair<Direction::forward>(view.iter());
        }

        /* Get a forward iterator to the head of the list. */
        inline auto begin() const {
            return Iterator<Direction::forward>(view.iter.begin());
        }

        /* Get an empty forward iterator to terminate the sequence. */
        inline auto end() const {
            return Iterator<Direction::forward>(view.iter.end());
        }

        /* Get a forward Python iterator over the list. */
        inline PyObject* python() const {
            auto iter = PyIterator<Direction::forward>::create();
            if (iter == nullptr) {
                PyErr_SetString(
                    PyExc_RuntimeError,
                    "could not create iterator instance"
                );
                return nullptr;
            }

            // initialize iterator
            iter->iter = (*this)();
            return static_cast<PyObject*>(iter);
        }

        /* Get a coupled reverse iterator over the list. */
        template <typename = std::enable_if<doubly_linked>>
        inline auto reverse() const {
            return IteratorPair<Direction::backward>(view.iter.reverse());
        }

        /* Get a backward iterator to the tail of the list. */
        template <typename = std::enable_if<doubly_linked>>
        inline auto rbegin() const {
            return Iterator<Direction::backward>(view.iter.rbegin());
        }

        /* Get an empty backward iterator to terminate the sequence. */
        template <typename = std::enable_if<doubly_linked>>
        inline auto rend() const {
            return Iterator<Direction::backward>(view.iter.rend());
        }

        /* Get a reverse Python iterator over the list. */
        template <typename = std::enable_if<doubly_linked>>
        inline PyObject* rpython() const {
            auto iter = PyIterator<Direction::backward>::create();
            if (iter == nullptr) {
                PyErr_SetString(
                    PyExc_RuntimeError,
                    "could not create iterator instance"
                );
                return nullptr;
            }

            // initialize iterator
            iter->iter = reverse();
            return static_cast<PyObject*>(iter);
        }

        /* A wrapper around a view iterator that yields values rather than nodes. */
        template <Direction dir>
        class Iterator {
            using ViewIter = typename View::template Iterator<dir>;

        public:
            /* iterator tags for std::iterator_traits */
            using iterator_category     = typename ViewIter::iterator_category;
            using difference_type       = typename ViewIter::difference_type;
            using value_type            = PyObject*;
            using pointer               = PyObject**;
            using reference             = PyObject*&;

            /* Dereference the iterator to get the value at the current node. */
            inline PyObject* operator*() const {
                return (*iter)->value;
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
            ViewIter iter;

            Iterator(ViewIter iter) : iter(iter) {}
        };

        /* A wrapper around a coupled iterator that combines separate begin() and end()
        iterators. */
        template <Direction dir>
        class IteratorPair {
            using ViewIterPair = typename View::template IteratorPair<dir>;
            using ViewIter = typename View::template Iterator<dir>;

        public:
            /* iterator tags for std::iterator_traits */
            using iterator_category     = typename ViewIterPair::iterator_category;
            using difference_type       = typename ViewIterPair::difference_type;
            using value_type            = PyObject*;
            using pointer               = PyObject**;
            using reference             = PyObject*&;

            /* Get the begin() iterator from the pair. */
            inline ViewIter& begin() const {
                return iter.begin();
            }

            /* Get the end() iterator from the pair. */
            inline ViewIter& end() const {
                return iter.end();
            }

            /* Dereference the iterator to get the value at the current node. */
            inline PyObject* operator*() const {
                return (*iter)->value;
            }

            /* Advance the iterator to the next node. */
            inline IteratorPair& operator++() {
                ++iter;
                return *this;
            }

            /* Inequality comparison to terminate the loop. */
            inline bool operator!=(const IteratorPair& other) const {
                return iter != other.iter;
            }

        private:
            friend IteratorFactory;
            ViewIterPair iter;

            IteratorPair(ViewIterPair iter) : iter(iter) {}
        };

        /* A Python-compatible forward iterator over the list contents. */
        template <Direction dir>
        struct PyIterator {
            PyObject_HEAD
            IteratorPair<dir> iter;

            /* Call next(iter) from Python. */
            static PyObject* iter_next(PyObject* self) {
                PyIterator* ref = static_cast<PyIterator*>(self);
                if (ref->iter == ref->iter.end()) {  // terminate the sequence
                    PyErr_SetNone(PyExc_StopIteration);
                    return nullptr;
                }

                // increment iterator and return current value
                PyObject* result = *(ref->iter);
                ++(ref->iter);
                return Py_NewRef(result);  // new reference
            }

            static constexpr PyTypeObject Type {
                PyVarObject_HEAD_INIT(nullptr, 0)
                .tp_name = (dir == Direction::forward ?
                    "LinkedList.iter" :
                    "LinkedList.iter_reverse"
                ),
                .tp_doc = (dir == Direction::forward ?
                    "Forward iterator over a LinkedList." :
                    "Reverse iterator over a LinkedList."
                ),
                .tp_basicsize = sizeof(PyIterator),
                .tp_itemsize = 0,
                .tp_flags = Py_TPFLAGS_DEFAULT,
                .tp_new = PyType_GenericNew,
                .tp_iter = PyObject_SelfIter,
                .tp_iternext = iter_next,
            };

        private:
            friend IteratorFactory;

            static PyIterator<dir>* create() {
                // lazily initialize python iterator type
                static bool initialized = false;
                if (!initialized) {
                    if (PyType_Ready(&PyIterator<dir>::Type) < 0) {
                        return nullptr;  // propagate error
                    }
                    initialized = true;
                }

                // create new iterator instance
                return PyObject_New(PyIterator<dir>, &PyIterator<dir>::Type);
            }
        };

    private:
        friend LinkedBase;
        View& view;

        IteratorFactory(View& view) : view(view) {}
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
    template <typename = std::enable_if<doubly_linked>>
    inline auto rbegin() const {
        return iter.rbegin();
    }

    /* Get a reverse iterator to the start of the list. */
    template <typename = std::enable_if<doubly_linked>>
    inline auto rend() const {
        return iter.rend();
    }

protected:

    // TODO: max_size should be std::optional<size_t> = std::nullopt

    /* Construct an empty list. */
    LinkedBase(long long max_size = -1, PyObject* spec = nullptr) :
        view(max_size, spec), iter(view)
    {}

    /* Construct a list from an input iterable. */
    LinkedBase(
        PyObject* iterable,
        bool reverse = false,
        long long max_size = -1,
        PyObject* spec = nullptr
    ) : view(iterable, reverse, max_size, spec), iter(view)
    {}

    /* Construct a list from a base view. */
    LinkedBase(View&& view) : view(view), iter(view) {}

    // TODO: construct from iterators?

    /* Copy constructor. */
    LinkedBase(const LinkedBase& other) :
        view(other.view), iter(view)
    {}

    /* Move constructor. */
    LinkedBase(LinkedBase&& other) :
        view(std::move(other.view)), iter(view)
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


////////////////////////////////////
////    FORWARD DECLARATIONS    ////
////////////////////////////////////


template <typename Derived, typename ViewType, typename SortPolicy>
class ListInterface_;


template <typename Derived>
class ListOps_;


//////////////////////
////    PUBLIC    ////
//////////////////////


template <
    typename NodeType = DoubleNode,
    template <typename> class AllocatorPolicy = DynamicAllocator,
    template <typename> class SortPolicy = MergeSort,
    typename LockPolicy = BasicLock
>
class LinkedList :
    public LinkedBase<ListView<NodeType, AllocatorPolicy>>,
    public ListInterface_<
        LinkedList<NodeType, AllocatorPolicy, SortPolicy, LockPolicy>,
        ListView<NodeType, AllocatorPolicy>,
        SortPolicy<ListView<NodeType, AllocatorPolicy>>
    >,
    public ListOps_<
        LinkedList<NodeType, AllocatorPolicy, SortPolicy, LockPolicy>
    >
{
public:
    using View = ListView<NodeType, AllocatorPolicy>;
    using Node = typename View::Node;

private:
    using Self = LinkedList<NodeType, AllocatorPolicy, SortPolicy, LockPolicy>;
    using Base = LinkedBase<View>;
    using Sort = SortPolicy<View>;
    using Lock = LockPolicy;
    using IList = ListInterface_<Self, View, Sort>;
    using ListOps = ListOps_<Self>;

public:

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct an empty list. */
    LinkedList(long long max_size = -1, PyObject* spec = nullptr) :
        Base(max_size, spec), IList(this->view)
    {}

    /* Construct a list from an input iterable. */
    LinkedList(
        PyObject* iterable,
        bool reverse = false,
        long long max_size = -1,
        PyObject* spec = nullptr
    ) : Base(iterable, reverse, max_size, spec), IList(this->view)
    {}

    /* Construct a list from a base view. */
    LinkedList(View&& view) :
        Base(view), IList(this->view)
    {}

    /* Copy constructor. */
    LinkedList(const LinkedList& other) :
        Base(other.view), IList(this->view)
    {}

    /* Move constructor. */
    LinkedList(LinkedList&& other) :
        Base(std::move(other.view)), IList(this->view)
    {}

    /* Copy assignment operator. */
    LinkedList& operator=(const LinkedList& other) {
        Base::operator=(other);
        return *this;
    }

    /* Move assignment operator. */
    LinkedList& operator=(LinkedList&& other) {
        Base::operator=(std::move(other));
        return *this;
    }

    /* A functor that allows the list to be locked for thread safety. */
    const Lock lock;
    /* BasicLock:
     * lock()
     * lock.context()
     *
     * DiagnosticLock:
     * lock()
     * lock.context()
     * lock.count()
     * lock.duration()
     * lock.contention()
     * lock.reset_diagnostics()
     */
};


//////////////////////
////    MIXINS    ////
//////////////////////


// TODO: list.position() should probably return a proxy for an iterator that can be
// implicitly converted to a value type, and has the public interface methods.  In
// fact, we could make position() completely internal to the view, and just use the
// array index operator publicly.


// TODO: IndexFactory might not even need to exist.  We could just put the
// normalize_index() method in LinkedBase as a protected method, and then have
// ListInterface_ call it in operator[].  This would remove a functor (and memory
// overhead) from the public class.


// slice() and iter() still need to be functors, but we don't need to make them any
// more complicated than they already are.


// We could just implement slice() directly within ListInterface.  This could avoid
// another functor, and would inline the full interface into the public class.


/* A mixin that implements the full Python list interface. */
template <typename Derived, typename ViewType, typename SortPolicy>
class ListInterface_ {
    using View = ViewType;
    using Node = typename View::Node;
    using ValueType = PyObject*;
    inline static constexpr bool doubly_linked = View::doubly_linked;

    template <Direction dir, typename = void>
    using ViewIter = typename View::template Iterator<dir>;

    class SliceIndices;

public:

    /* Append an item to the end of a list. */
    void append(PyObject* item, const bool left = false) {
        View& view = self().view;

        // allocate a new node
        Node* node = view.node(item);
        if (node == nullptr) {
            return;  // propagate error
        }

        // link to beginning/end of list
        if (left) {
            view.link(nullptr, node, view.head);
        } else {
            view.link(view.tail, node, nullptr);
        }
        if (PyErr_Occurred()) {
            view.recycle(node);  // clean up allocated node
        }
    }

    /* Insert an item into a list at the specified index. */
    template <typename T>
    void insert(T index, PyObject* item) {
        (*this)[index].insert(item);
    }

    /* Extend a list by appending elements from the iterable. */
    void extend(PyObject* items, const bool left = false) {
        View& view = self().view;

        // note original head/tail in case of error
        Node* original;
        if (left) {
            original = view.head;
        } else {
            original = view.tail;
        }

        // proceed with extend
        try {
            PyIterable sequence(items);
            for (PyObject* item : sequence) {
                // allocate a new node
                Node* node = view.node(item);
                if (node == nullptr) {
                    throw std::invalid_argument("could not allocate node");
                }

                // link to beginning/end of list
                if (left) {
                    view.link(nullptr, node, view.head);
                } else {
                    view.link(view.tail, node, nullptr);
                }
                if (PyErr_Occurred()) {
                    view.recycle(node);  // clean up allocated node
                    throw std::invalid_argument("could not link node");
                }
            }
        } catch (std::invalid_argument&) {
            // NOTE: this branch can also be triggered if the iterator raises an
            // exception during `iter()` or `next()`.
            if (left) {
                // if we added nodes to the left, then we just remove until we reach
                // the original head
                Node* curr = view.head;
                while (curr != original) {
                    Node* next = static_cast<Node*>(curr->next);
                    view.unlink(nullptr, curr, curr->next);
                    view.recycle(curr);
                    curr = next;
                }
            } else {
                // otherwise, we start from the original tail and remove until we reach
                // the end of the list
                Node* curr = static_cast<Node*>(original->next);
                while (curr != nullptr) {
                    Node* next = static_cast<Node*>(curr->next);
                    view.unlink(original, curr, next);
                    view.recycle(curr);
                    curr = next;
                }
            }
            return;  // propagate Python error
        }
    }

    /* Get the index of an item within a list. */
    template <typename T>
    size_t index(PyObject* item, T start = 0, T stop = -1) const {
        const View& view = self().view;

        // trivial case: empty list
        if (view.size == 0) {
            std::ostringstream msg;
            msg << repr(item) << " is not in list";
            throw std::invalid_argument(msg.str());
        }

        // normalize start/stop indices
        size_t norm_start = normalize_index(start, true);
        size_t norm_stop = normalize_index(stop, true);
        if (norm_start > norm_stop) {
            throw std::invalid_argument(
                "start index cannot be greater than stop index"
            );
        }

        // if list is doubly-linked and stop is closer to tail than start is to head,
        // then we iterate backward from the tail
        if constexpr (doubly_linked) {
            if ((view.size - 1 - norm_stop) < norm_start) {
                // get backwards iterator to stop index
                auto iter = view.rbegin();
                size_t idx = view.size - 1;
                while (idx >= norm_stop) {
                    ++iter;
                    --idx;
                }

                // search until we hit start index
                bool found = false;
                size_t last_observed;
                while (idx >= norm_start) {
                    int comp = PyObject_RichCompareBool((*iter)->value, item, Py_EQ);
                    if (comp == -1) {
                        throw std::runtime_error("could not compare item");
                    } else if (comp == 1) {
                        found = true;
                        last_observed = idx;
                    }
                    ++iter;
                    --idx;
                }
                if (found) {
                    return last_observed;
                }
                std::ostringstream msg;
                msg << repr(item) << " is not in list";
                throw std::invalid_argument(msg.str());
            }
        }

        // otherwise, we iterate forward from the head
        auto iter = view.begin();
        size_t idx = 0;
        while (idx < norm_start) {
            ++iter;
            ++idx;
        }

        // search until we hit item or stop index
        while (idx < norm_stop) {
            int comp = PyObject_RichCompareBool((*iter)->value, item, Py_EQ);
            if (comp == -1) {
                throw std::runtime_error("could not compare item");
            } else if (comp == 1) {
                return idx;
            }
            ++iter;
            ++idx;
        }
        std::ostringstream msg;
        msg << repr(item) << " is not in list";
        throw std::invalid_argument(msg.str());
    }

    /* Count the number of occurrences of an item within a list. */
    template <typename T>
    size_t count(PyObject* item, T start = 0, T stop = -1) const {
        const View& view = self().view;

        // trivial case: empty list
        if (view.size == 0) {
            return 0;
        }

        // normalize start/stop indices
        size_t norm_start = normalize_index(start, true);
        size_t norm_stop = normalize_index(stop, true);
        if (norm_start > norm_stop) {
            throw std::invalid_argument(
                "start index cannot be greater than stop index"
            );
        }

        // if list is doubly-linked and stop is closer to tail than start is to head,
        // then we iterate backward from the tail
        if constexpr (doubly_linked) {
            if ((view.size - 1 - norm_stop) < norm_start) {
                // get backwards iterator to stop index
                auto iter = view.iter.reverse();
                size_t idx = view.size - 1;
                while (idx > norm_stop) {
                    ++iter;
                    --idx;
                }

                // search until we hit start index
                size_t count = 0;
                while (idx >= norm_start) {
                    int comp = PyObject_RichCompareBool((*iter)->value, item, Py_EQ);
                    if (comp == -1) {
                        throw std::runtime_error("could not compare item");
                    } else if (comp == 1) {
                        ++count;
                    }
                    ++iter;
                    --idx;
                }
                return count;
            }
        }

        // otherwise, we iterate forward from the head
        auto iter = view.iter();
        size_t idx = 0;
        while (idx < norm_start) {
            ++iter;
            ++idx;
        }

        // search until we hit item or stop index
        size_t count = 0;
        while (idx < norm_stop) {
            int comp = PyObject_RichCompareBool((*iter)->value, item, Py_EQ);
            if (comp == -1) {
                throw std::runtime_error("could not compare item");
            } else if (comp == 1) {
                ++count;
            }
            ++iter;
            ++idx;
        }
        return count;
    }

    /* Check if the list contains a certain item. */
    bool contains(PyObject* item) const {
        const View& view = self().view;

        for (auto node : view) {
            // C API equivalent of the == operator
            int comp = PyObject_RichCompareBool(node->value, item, Py_EQ);
            if (comp == -1) {  // == comparison raised an exception
                throw std::runtime_error("could not compare item");
            } else if (comp == 1) {  // found a match
                return true;
            }
        }

        // item not found
        return false;
    }

    /* Remove the first occurrence of an item from a list. */
    void remove(PyObject* item) {
        View& view = self().view;

        // find item in list
        for (auto iter = view.iter(); iter != iter.end(); ++iter) {
            // C API equivalent of the == operator
            int comp = PyObject_RichCompareBool((*iter)->value, item, Py_EQ);
            if (comp == -1) {  // == comparison raised an exception
                return;  // propagate error
            } else if (comp == 1) {  // found a match
                view.recycle(iter.remove());
                return;
            }
        }

        // item not found
        PyErr_Format(PyExc_ValueError, "%R is not in list", item);        
    }

    /* Remove an item from a list and return its value. */
    template <typename T>
    PyObject* pop(T index) {
        return (*this)[index].pop();
    }

    /* Remove all elements from a list. */
    void clear() {
        self().view.clear();
    }

    /* Return a shallow copy of the list. */
    std::optional<Derived> copy() const {
        std::optional<View> view = self().view.copy();
        if (!view.has_value()) {
            return std::nullopt;  // propagate error
        }
        return std::make_optional(Derived(view.value()));
    }

    /* Reverse a list in-place. */
    void reverse() {
        View& view = self().view;

        // save original `head` pointer
        Node* head = view.head;
        Node* curr = head;
        
        if constexpr (doubly_linked) {
            // swap all `next`/`prev` pointers
            while (curr != nullptr) {
                Node* next = static_cast<Node*>(curr->next);
                curr->next = static_cast<Node*>(curr->prev);
                curr->prev = next;
                curr = next;
            }
        } else {
            // swap all `next` pointers
            Node* prev = nullptr;
            while (curr != nullptr) {
                Node* next = static_cast<Node*>(curr->next);
                curr->next = prev;
                prev = curr;
                curr = next;
            }
        }

        // swap `head`/`tail` pointers
        view.head = view.tail;
        view.tail = head;
    }

    /* Rotate a list to the right by the specified number of steps. */
    void rotate(long long steps = 1) {
        View& view = self().view;

        // normalize steps
        size_t norm_steps = llabs(steps) % view.size;
        if (norm_steps == 0) {
            return;  // rotated list is identical to original
        }

        // get index at which to split the list
        size_t index;
        size_t rotate_left = (steps < 0);
        if (rotate_left) {  // count from head
            index = norm_steps;
        } else {  // count from tail
            index = view.size - norm_steps;
        }

        Node* new_head;
        Node* new_tail;

        // identify new head and tail of rotated list
        if constexpr (doubly_linked) {
            // NOTE: if the list is doubly-linked, then we can iterate in either
            // direction to find the junction point.
            if (index > view.size / 2) {  // backward traversal
                new_head = view.tail;
                for (size_t i = view.size - 1; i > index; i--) {
                    new_head = static_cast<Node*>(new_head->prev);
                }
                new_tail = static_cast<Node*>(new_head->prev);

                // split list at junction and join previous head/tail
                Node::split(new_tail, new_head);
                Node::join(view.tail, view.head);

                // update head/tail pointers
                view.head = new_head;
                view.tail = new_tail;
                return;
            }
        }

        // forward traversal
        new_tail = view.head;
        for (size_t i = 1; i < index; i++) {
            new_tail = static_cast<Node*>(new_tail->next);
        }
        new_head = static_cast<Node*>(new_tail->next);

        // split at junction and join previous head/tail
        Node::split(new_tail, new_head);
        Node::join(view.tail, view.head);

        // update head/tail pointers
        view.head = new_head;
        view.tail = new_tail;
    }

    // forward declarations
    class ElementProxy;
    class SliceProxy;

    /* Get a proxy for a value at a particular index of the list. */
    template <typename T>
    ElementProxy operator[](T index) {
        // normalize index (can throw std::out_of_range, std::bad_typeid)
        size_t norm_index = normalize_index(index);

        // get iterator to index
        View& view = self().view;
        if constexpr (doubly_linked) {
            if (norm_index > (view.size - (view.size > 0)) / 2) {  // backward traversal
                ViewIter<Direction::backward> iter = view.rbegin();
                for (size_t i = view.size - 1; i > norm_index; --i) {
                    ++iter;
                }
                return ElementProxy(view, Bidirectional(iter));
            }
        }

        // forward traversal
        ViewIter<Direction::forward> iter = view.begin();
        for (size_t i = 0; i < norm_index; ++i) {
            ++iter;
        }
        return ElementProxy(view, Bidirectional(iter));
    }

    /* Get a proxy for a slice within the list. */
    template <typename... Args>
    SliceProxy slice(Args&&... args) {
        // can throw std::bad_typeid, std::invalid_argument, std::runtime_error
        return SliceProxy(
            self().view,
            normalize_indices(std::forward<Args>(args)...)
        );
    }

    ////////////////////////
    ////    FUNCTORS    ////
    ////////////////////////

    /* A functor that sorts the list in place. */
    const SortPolicy sort;
    /* sort(PyObject* key = nullptr, bool reverse = false)
     */

    ///////////////////////
    ////    PROXIES    ////
    ///////////////////////

    /* A proxy for an element at a particular index of the list, as returned by the []
    operator. */
    class ElementProxy {
    public:

        /* Get the value at the current index. */
        inline ValueType& get() const {
            return (*iter)->value;
        }

        /* Set the value at the current index. */
        inline void set(const ValueType& value) {
            Node* node = view.node(value);
            if (node == nullptr) {
                return;  // propagate error
            }
            iter.replace(node);
        }

        /* Insert a value at the current index. */
        inline void insert(const ValueType& value) {
            // allocate a new node
            Node* node = view.node(value);
            if (node == nullptr) {
                return;  // propagate error
            }

            // insert node at current index
            iter.insert(node);
            if (PyErr_Occurred()) {
                view.recycle(node);  // clean up allocated node
            }
        }

        /* Delete the value at the current index. */
        inline void del() {
            iter.drop();
        }

        /* Remove the node at the current index and return its value. */
        inline ValueType pop() {
            Node* node = iter.remove();
            ValueType result = node->value;
            Py_INCREF(result);  // ensure value is not garbage collected during recycle()
            view.recycle(node);
            return result;
        }

        /* Implicitly convert the proxy to the value where applicable.

        This is syntactic sugar for get() such that `ValueType value = list[i]` is
        equivalent to `ValueType value = list[i].get()`.  The same implicit conversion
        is also applied if the proxy is passed to a function that expects a value,
        unless that function is marked as `explicit`. */
        inline operator ValueType() const {
            return get();
        }

        /* Assign the value at the current index.

        This is syntactic sugar for set() such that `list[i] = value` is equivalent to
        `list[i].set(value)`. */
        inline ElementProxy& operator=(const ValueType& value) {
            set(value);
            return *this;
        }

    private:
        friend ListInterface_;
        View& view;
        Bidirectional<ViewIter> iter;

        template <Direction dir>
        ElementProxy(View& view, ViewIter<dir>& iter) : view(view), iter(iter) {}
    };

    /* A proxy for a slice within a list, as returned by the slice() factory method. */
    class SliceProxy {
    public:

        ///////////////////////
        ////    INDICES    ////
        ///////////////////////

        /* Pass through to SliceIndices. */
        inline long long start() const { return indices.start; }
        inline long long stop() const { return indices.stop; }
        inline long long step() const { return indices.step; }
        inline size_t abs_step() const { return indices.abs_step; }
        inline size_t first() const { return indices.first; }
        inline size_t last() const { return indices.last; }
        inline size_t length() const { return indices.length; }
        inline bool empty() const { return indices.length == 0; }
        inline bool backward() const { return indices.backward; }
        inline bool inverted() const { return indices.inverted; }

        //////////////////////
        ////    PUBLIC    ////
        //////////////////////

        /* Extract a slice from a linked list. */
        Derived get() const {
            // allocate a new list to hold the slice
            Py_ssize_t max_size = view.max_size; 
            if (max_size >= 0) {
                max_size = static_cast<Py_ssize_t>(length());
            }
            View result(max_size, view.specialization);

            // if slice is empty, return empty view
            if (empty()) {
                return Derived(std::move(result));
            }

            // copy nodes from original view into result
            for (auto node : *this) {
                Node* copy = result.copy(node);
                if (copy == nullptr) {
                    throw std::runtime_error("could not copy node");
                }

                // link to slice
                if (inverted()) {
                    result.link(nullptr, copy, result.head);
                } else {
                    result.link(result.tail, copy, nullptr);
                }
                if (PyErr_Occurred()) {
                    result.recycle(copy);  // clean up staged node
                    throw std::runtime_error("could not link node");
                }
            }

            return Derived(std::move(result));
        }

        /* Replace a slice within a linked list. */
        void set(PyObject* items) {
            // unpack iterable into reversible sequence
            PyObject* sequence = PySequence_Fast(items, "can only assign an iterable");
            if (sequence == nullptr) {
                return;  // propagate TypeError: can only assign an iterable
            }

            // check for no-op
            size_t seq_length = static_cast<size_t>(PySequence_Fast_GET_SIZE(sequence));
            if (empty() && seq_length == 0) {
                Py_DECREF(sequence);
                return;
            }

            // check slice length matches sequence length
            if (length() != seq_length && step() != 1) {
                // NOTE: Python allows forced insertion if and only if the step size is 1
                PyErr_Format(
                    PyExc_ValueError,
                    "attempt to assign sequence of size %zu to extended slice of size %zu",
                    seq_length,
                    length()
                );
                Py_DECREF(sequence);
                return;
            }

            // allocate recovery array
            RecoveryArray recovery(length());
            if (PyErr_Occurred()) {  // error during array allocation
                Py_DECREF(sequence);
                return;
            }

            // loop 1: remove current nodes in slice
            for (auto iter = iter(); iter != iter.end(); ++iter) {
                Node* node = iter.remove();  // remove node from list
                Node::init_copy(&recovery[iter.index()], node);  // copy to recovery array
                view.recycle(node);  // recycle original node
            }

            // loop 2: insert new nodes from sequence into vacated slice
            for (auto iter = iter(seq_length); iter != iter.end(); ++iter) {
                // NOTE: PySequence_Fast_GET_ITEM() returns a borrowed reference (no
                // DECREF required)
                PyObject* item;
                if (inverted()) {  // count from the back
                    size_t idx = seq_length - 1 - iter.index();
                    item = PySequence_Fast_GET_ITEM(sequence, idx);
                } else {  // count from the front
                    item = PySequence_Fast_GET_ITEM(sequence, iter.index());
                }

                // allocate a new node for the item
                Node* new_node = view.node(item);
                if (new_node == nullptr) {
                    undo_set_slice(slice, recovery, iter.index());
                    Py_DECREF(sequence);
                    return;
                }

                // insert node into slice at current index
                iter.insert(new_node);
                if (PyErr_Occurred()) {
                    view.recycle(new_node);
                    undo_set_slice(slice, recovery, iter.index());
                    Py_DECREF(sequence);
                    return;
                }
            }

            // loop 3: deallocate removed nodes
            for (size_t i = 0; i < length(); i++) {
                Node::teardown(&recovery[i]);
            }

            Py_DECREF(sequence);
        }

        /* Delete a slice within a linked list. */
        void del() {
            // check for no-op
            if (empty()) {
                return;
            }

            // recycle every node in slice
            for (auto iter = iter(); iter != iter.end(); ++iter) {
                Node* node = iter.remove();
                view.recycle(node);
            }
        }

        /////////////////////////////////
        ////    ITERATOR PROTOCOL    ////
        /////////////////////////////////

        // NOTE: Reverse iterators are only compiled for doubly-linked lists.

        template <
            Direction dir = Direction::forward,
            typename = std::enable_if_t<dir == Direction::forward || doubly_linked>
        >
        class Iterator;
        using IteratorPair = CoupledIterator<Bidirectional<Iterator>>;

        /* Return a coupled pair of iterators with a possible length override. */
        inline IteratorPair iter(std::optional<size_t> length = std::nullopt) const {
            using Forward = Iterator<Direction::forward>;

            // default to length of slice
            if (!length.has_value()) {
                return IteratorPair(begin(), end());  
            }

            // use length override if given
            size_t len = length.value();

            // backward traversal
            if constexpr (doubly_linked) {
                using Backward = Iterator<Direction::backward>;
                if (backward()) {
                    return IteratorPair(
                        Bidirectional(Backward(view, origin(), indices, len)),
                        Bidirectional(Backward(view, indices, len))
                    );
                }
            }

            // forward traversal
            return IteratorPair(
                Bidirectional(Forward(view, origin(), indices, len)),
                Bidirectional(Forward(view, indices, len))
            );
        }

        /* Return an iterator to the start of the slice. */
        inline Bidirectional<Iterator> begin() const {
            using Forward = Iterator<Direction::forward>;

            // account for empty sequence
            if (empty()) {
                return Bidirectional(Forward(view, indices, length()));
            }

            // backward traversal
            if constexpr (doubly_linked) {
                using Backward = Iterator<Direction::backward>;
                if (backward()) {
                    return Bidirectional(Backward(view, origin(), indices, length()));
                }
            }

            // forward traversal
            return Bidirectional(Forward(view, origin(), indices, length()));        
        }

        /* Return an iterator to the end of the slice. */
        inline Bidirectional<Iterator> end() const {
            using Forward = Iterator<Direction::forward>;

            // return same orientation as begin()
            if (empty()) {
                return Bidirectional(Forward(view, indices, length()));
            }

            // backward traversal
            if constexpr (doubly_linked) {
                using Backward = Iterator<Direction::backward>;
                if (backward()) {
                    return Bidirectional(Backward(view, indices, length()));
                }
            }

            // forward traversal
            return Bidirectional(Forward(view, indices, length()));
        }

        /* A specialized iterator built for slice traversal. */
        template <Direction dir, typename>
        class Iterator : public IndexFactory<View>::template Iterator<dir> {
        public:
            using Base = typename IndexFactory<View>::template Iterator<dir>;

            /* Prefix increment to advance the iterator to the next node in the slice. */
            inline Iterator& operator++() {
                ++this->idx;
                if (this->idx == length_override) {
                    return *this;  // don't jump on last iteration
                }

                if constexpr (dir == Direction::backward) {
                    for (size_t i = implicit_skip; i < indices.abs_step(); ++i) {
                        this->next = this->curr;
                        this->curr = this->prev;
                        this->prev = static_cast<Node*>(this->curr->prev);
                    }
                } else {
                    for (size_t i = implicit_skip; i < indices.abs_step(); ++i) {
                        this->prev = this->curr;
                        this->curr = this->next;
                        this->next = static_cast<Node*>(this->curr->next);
                    }
                }
                return *this;
            }

            /* Inequality comparison to terminate the slice. */
            template <Direction T>
            inline bool operator!=(const Iterator<T>& other) const {
                return idx != other.idx;
            }

            //////////////////////////////
            ////    HELPER METHODS    ////
            //////////////////////////////

            /* Get the current index of the iterator within the list. */
            inline size_t index() const {
                return idx;
            }

            /* Remove the node at the current position. */
            inline Node* remove() { ++implicit_skip; return Base::remove(); }

            /* Copy constructor. */
            Iterator(const Iterator& other) :
                Base(other), indices(other.indices), length_override(other.length_override),
                implicit_skip(other.implicit_skip)
            {}

            /* Move constructor. */
            Iterator(Iterator&& other) :
                Base(std::move(other)), indices(std::move(other.indices)),
                length_override(other.length_override),
                implicit_skip(other.implicit_skip)
            {}

        protected:
            friend SliceProxy;
            size_t idx;
            const SliceIndices& indices;
            size_t length_override;
            size_t implicit_skip;

            ////////////////////////////
            ////    CONSTRUCTORS    ////
            ////////////////////////////

            /* Get an iterator to the start of the slice. */
            Iterator(
                View& view,
                Node* origin,
                const SliceIndices& indices,
                size_t length_override
            ) :
                Base(view, nullptr, 0), indices(indices), length_override(length_override),
                implicit_skip(0)
            {
                if constexpr (dir == Direction::backward) {
                    this->next = origin;
                    if (this->next == nullptr) {
                        this->curr = this->view.tail;
                    } else {
                        this->curr = static_cast<Node*>(this->next->prev);
                    }
                    if (this->curr != nullptr) {
                        this->prev = static_cast<Node*>(this->curr->prev);
                    }
                } else {
                    this->prev = origin;
                    if (this->prev == nullptr) {
                        this->curr = this->view.head;
                    } else {
                        this->curr = static_cast<Node*>(this->prev->next);
                    }
                    if (this->curr != nullptr) {
                        this->next = static_cast<Node*>(this->curr->next);
                    }
                }
            }

            /* Get an iterator to terminate the slice. */
            Iterator(View& view, const SliceIndices& indices, size_t length_override) :
                Base(view, length_override), indices(indices),
                length_override(length_override), implicit_skip(0)
            {}

        };


    private:
        friend ListInterface_;
        View& view;
        const SliceIndices indices;
        mutable bool found;  // indicates whether we've cached the origin node
        mutable Node* _origin;  // node that immediately precedes slice (can be NULL)

        /* Construct a SliceProxy with at least one element. */
        SliceProxy(View& view, SliceIndices&& indices) :
            view(view), indices(indices), _origin(nullptr), found(false)
        {}

        /* Find and cache the origin node for the slice. */
        Node* origin() const {
            if (found) {
                return _origin;
            }

            // find origin node
            if constexpr (doubly_linked) {
                if (backward()) {  // backward traversal
                    Node* next = nullptr;
                    Node* curr = view.tail;
                    for (size_t i = view.size - 1; i > first(); i--) {
                        next = curr;
                        curr = static_cast<Node*>(curr->prev);
                    }
                    found = true;
                    _origin = next;
                    return _origin;
                }
            }

            // forward traversal
            Node* prev = nullptr;
            Node* curr = view.head;
            for (size_t i = 0; i < first(); i++) {
                prev = curr;
                curr = static_cast<Node*>(curr->next);
            }
            found = true;
            _origin = prev;
            return _origin;
        }

        /* A raw, contiguous memory block that nodes can be copied into and out of in case
        of an error. */
        struct RecoveryArray {
            Node* nodes;
            size_t length;

            /* Allocate a contiguous array of nodes. */
            RecoveryArray(size_t length) : length(length) {
                nodes = static_cast<Node*>(malloc(sizeof(Node) * length));
                if (nodes == nullptr) {
                    throw std::bad_alloc();
                }
            }

            /* Tear down all nodes and free the recovery array. */
            ~RecoveryArray() {
                if (nodes != nullptr) {
                    free(nodes);
                }
            }

            /* Index the array to access a particular node. */
            Node& operator[](size_t index) {
                return nodes[index];
            }

        };

        /* Undo a call to Slice::replace() in the event of an error. */
        void undo_set_slice(RecoveryArray& recovery, size_t n_staged) {
            // loop 3: remove nodes that have already been added to slice
            for (auto iter = iter(n_staged); iter != iter.end(); ++iter) {
                Node* node = iter.remove();  // remove node from list
                view.recycle(node);  // return node to allocator
            }

            // loop 4: reinsert original nodes
            for (auto iter = iter(); iter != iter.end(); ++iter) {
                Node* node = view.copy(&recovery[iter.index()]);  // copy from recovery
                Node::teardown(&recovery[iter.index()]);  // release recovery node
                iter.insert(node);  // insert into list
            }
        }

    };

private:

    /* A simple class representing the normalized indices needed to construct a
    coherent slice. */
    class SliceIndices {
    public:

        /* Get the original indices that were supplied to the constructor. */
        const long long start;
        const long long stop;
        const long long step;
        const size_t abs_step;

        /* Get the first and last included indices. */
        size_t first;
        size_t last;

        /* Get the number of items included in the slice. */
        const size_t length;

        /* Check if the first and last indices conform to the expected step size. */
        bool inverted;
        bool backward;

        /* Copy constructor. */
        SliceIndices(const SliceIndices& other) :
            start(other.start), stop(other.stop), step(other.step),
            abs_step(other.abs_step), first(other.first), last(other.last),
            length(other.length), inverted(other.inverted), backward(other.backward)
        {}

        /* Move constructor. */
        SliceIndices(SliceIndices&& other) :
            start(other.start), stop(other.stop), step(other.step),
            abs_step(other.abs_step), first(other.first), last(other.last),
            length(other.length), inverted(other.inverted), backward(other.backward)
        {}

        /* Assignment operators deleted due to presence of const members. */
        SliceIndices& operator=(const SliceIndices& other) = delete;
        SliceIndices& operator=(SliceIndices&& other) = delete;

    private:
        friend ListInterface_;

        SliceIndices(
            const long long start,
            const long long stop,
            const long long step,
            const size_t length,
            const size_t view_size
        ) : start(start), stop(stop), step(step), abs_step(llabs(step)),
            first(0), last(0), length(length), inverted(false), backward(false)
        {
            // convert to closed interval [start, closed]
            long long mod = py_modulo((stop - start), step);
            long long closed = (mod == 0) ? (stop - step) : (stop - mod);

            // get direction to traverse slice based on singly-/doubly-linked status
            std::pair<size_t, size_t> dir = slice_direction(closed, view_size);
            first = dir.first;
            last = dir.second;

            // Because we've adjusted our indices to minimize total iterations, we might
            // not be iterating in the same direction as the step size would indicate.
            // We must account for this when getting/setting items in the slice.
            backward = (first > ((view_size - (view_size > 0)) / 2));
            inverted = backward ^ (step < 0);
        }

        /* A Python-style modulo operator (%). */
        template <typename T>
        inline static T py_modulo(T a, T b) {
            // NOTE: Python's `%` operator is defined such that the result has
            // the same sign as the divisor (b).  This differs from C/C++, where
            // the result has the same sign as the dividend (a).
            return (a % b + b) % b;
        }

        /* Swap the start and stop indices based on singly-/doubly-linked status. */
        std::pair<long long, long long> slice_direction(long long closed, size_t view_size) {
            // if doubly-linked, start at whichever end is closest to slice boundary
            if constexpr (doubly_linked) {
                long long size = static_cast<long long>(view_size);
                if (
                    (step > 0 && start <= size - closed) ||
                    (step < 0 && size - start <= closed)
                ) {
                    return std::make_pair(start, closed);
                }
                return std::make_pair(closed, start);
            }

            // if singly-linked, always start from head of list
            if (step > 0) {
                return std::make_pair(start, closed);
            }
            return std::make_pair(closed, start);
        }
    };

    /* Enable access to members of the Derived type. */
    inline Derived& self() {
        return static_cast<Derived&>(*this);
    }

    /* Enable access to members of the Derived type in a const context. */
    inline const Derived& self() const {
        return static_cast<const Derived&>(*this);
    }

protected:

    ListInterface_(View& view) : sort(view) {}

    /* Normalize a numeric index, applying Python-style wraparound and bounds
    checking. */
    template <typename T>
    size_t normalize_index(T index, bool truncate = false) const {
        const View& view = self().view;

        // wraparound negative indices
        bool lt_zero = index < 0;
        if (lt_zero) {
            index += view.size;
            lt_zero = index < 0;
        }

        // boundscheck
        if (lt_zero || index >= static_cast<T>(view.size)) {
            if (truncate) {
                if (lt_zero) {
                    return 0;
                }
                return view.size - 1;
            }
            throw std::out_of_range("list index out of range");
        }

        // return as size_t
        return static_cast<size_t>(index);
    }

    /* Normalize a Python integer for use as an index to the list. */
    size_t normalize_index(PyObject* index, bool truncate = false) const {
        // check that index is a Python integer
        if (!PyLong_Check(index)) {
            throw std::bad_typeid("index must be a Python integer");
        }

        const View& view = self().view;

        // comparisons are kept at the python level until we're ready to return
        PyObject* py_zero = PyLong_FromSize_t(0);  // new reference
        PyObject* py_size = PyLong_FromSize_t(view.size);  // new reference
        int lt_zero = PyObject_RichCompareBool(index, py_zero, Py_LT);

        // wraparound negative indices
        bool release_index = false;
        if (lt_zero) {
            index = PyNumber_Add(index, py_size);  // new reference
            lt_zero = PyObject_RichCompareBool(index, py_zero, Py_LT);
            release_index = true;  // remember to DECREF index later
        }

        // boundscheck - value is bad
        if (lt_zero || PyObject_RichCompareBool(index, py_size, Py_GE)) {
            Py_DECREF(py_zero);
            Py_DECREF(py_size);
            if (release_index) {
                Py_DECREF(index);
            }

            // apply truncation if directed
            if (truncate) {
                if (lt_zero) {
                    return 0;
                }
                return view.size - 1;
            }

            // raise IndexError
            throw std::out_of_range("list index out of range");
        }

        // value is good - cast to size_t
        size_t result = PyLong_AsSize_t(index);

        // clean up references
        Py_DECREF(py_zero);
        Py_DECREF(py_size);
        if (release_index) {
            Py_DECREF(index);
        }

        return result;
    }

    /* Normalize slice indices, applying Python-style wraparound and bounds
    checking. */
    SliceIndices normalize_slice(
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt,
        std::optional<long long> step = std::nullopt
    ) const {
        // normalize slice indices
        long long size = static_cast<long long>(self().size());
        long long default_start = (step.value_or(0) < 0) ? (size - 1) : (0);
        long long default_stop = (step.value_or(0) < 0) ? (-1) : (size);
        long long default_step = 1;

        // normalize step
        long long step_ = step.value_or(default_step);
        if (step_ == 0) {
            throw std::invalid_argument("slice step cannot be zero");
        }

        // normalize start index
        long long start_ = start.value_or(default_start);
        if (start_ < 0) {
            start_ += size;
            if (start_ < 0) {
                start_ = (step_ < 0) ? (-1) : (0);
            }
        } else if (start_ >= size) {
            start_ = (step_ < 0) ? (size - 1) : (size);
        }

        // normalize stop index
        long long stop_ = stop.value_or(default_stop);
        if (stop_ < 0) {
            stop_ += size;
            if (stop_ < 0) {
                stop_ = (step_ < 0) ? -1 : 0;
            }
        } else if (stop_ > size) {
            stop_ = (step_ < 0) ? (size - 1) : (size);
        }

        // get length of slice
        size_t length = std::max(
            (stop_ - start_ + step_ - (step_ > 0 ? 1 : -1)) / step_,
            static_cast<long long>(0)
        );

        // return as SliceIndices
        return SliceIndices(start_, stop_, step_, length, size);
    }

    /* Normalize a Python slice object, applying Python-style wraparound and bounds
    checking. */
    SliceIndices normalize_slice(PyObject* py_slice) const {
        // check that input is a Python slice object
        if (!PySlice_Check(py_slice)) {
            throw std::bad_typeid("index must be a Python slice");
        }

        size_t size = self().size();

        // use CPython API to get slice indices
        Py_ssize_t py_start, py_stop, py_step, py_length;
        int err = PySlice_GetIndicesEx(
            py_slice, size, &py_start, &py_stop, &py_step, &py_length
        );
        if (err == -1) {
            throw std::runtime_error("failed to normalize slice");
        }

        // cast from Py_ssize_t
        long long start = static_cast<long long>(py_start);
        long long stop = static_cast<long long>(py_stop);
        long long step = static_cast<long long>(py_step);
        size_t length = static_cast<size_t>(py_length);

        // return as SliceIndices
        return SliceIndices(start, stop, step, length, size);
    }

};


/* A mixin that adds operator overloads that mimic the behavior of Python lists with
respect to concatenation, repetition, and lexicographic comparison. */
template <typename Derived>
class ListOps_ {
public:

    // NOTE: operator overloads are defined as non-member functions to allow for
    // symmetric operation between lists and other iterables.

    /* Overload the + operator to allow concatenation of Derived types from both
    Python and C++. */
    template <typename T>
    friend Derived operator+(const Derived& lhs, const T& rhs);
    template <typename T>
    friend T operator+(const T& lhs, const Derived& rhs);

    /* Overload the += operator to allow in-place concatenation of Derived types from
    both Python and C++. */
    template <typename T>
    friend Derived& operator+=(Derived& lhs, const T& rhs);

    // NOTE: We use a dummy typename to avoid forward declarations of operator* and
    // operator*=.  It doesn't actually affect the implementation of either overload.

    /* Overload the * operator to allow repetition of Derived types from both Python
    and C++. */
    template <typename>
    friend Derived operator*(const Derived& lhs, const ssize_t rhs);
    template <typename>
    friend Derived operator*(const Derived& lhs, const PyObject* rhs);
    template <typename>
    friend Derived operator*(const ssize_t lhs, const Derived& rhs);
    template <typename>
    friend Derived operator*(const PyObject* lhs, const Derived& rhs);

    /* Overload the *= operator to allow in-place repetition of Derived types from
    both Python and C++. */
    template <typename>
    friend Derived& operator*=(Derived& lhs, const ssize_t rhs);
    template <typename>
    friend Derived& operator*=(Derived& lhs, const PyObject* rhs);

    /* Overload the < operator to allow lexicographic comparison between Derived types
    and arbitrary C++ containers/Python sequences. */
    template <typename T>
    friend bool operator<(const Derived& lhs, const T& rhs);
    template <typename T>
    friend bool operator<(const T& lhs, const Derived& rhs);

    /* Overload the <= operator to allow lexicographic comparison between Derived types
    and arbitrary C++ containers/Python sequences. */
    template <typename T>
    friend bool operator<=(const Derived& lhs, const T& rhs);
    template <typename T>
    friend bool operator<=(const T& lhs, const Derived& rhs);

    /* Overload the == operator to allow lexicographic comparison between Derived types
    and arbitrary C++ containers/Python sequences. */
    template <typename T>
    friend bool operator==(const Derived& lhs, const T& rhs);
    template <typename T>
    friend bool operator==(const T& lhs, const Derived& rhs);

    /* Overload the != operator to allow lexicographic comparison between Derived types
    and arbitrary C++ containers/Python sequences. */
    template <typename T>
    friend bool operator!=(const Derived& lhs, const T& rhs);
    template <typename T>
    friend bool operator!=(const T& lhs, const Derived& rhs);

    /* Overload the > operator to allow lexicographic comparison between Derived types
    and arbitrary C++ containers/Python sequences. */
    template <typename T>
    friend bool operator>(const Derived& lhs, const T& rhs);
    template <typename T>
    friend bool operator>(const T& lhs, const Derived& rhs);

    /* Overload the >= operator to allow lexicographic comparison between Derived types
    and arbitrary C++ containers/Python sequences. */
    template <typename T>
    friend bool operator>=(const Derived& lhs, const T& rhs);
    template <typename T>
    friend bool operator>=(const T& lhs, const Derived& rhs);


    // TODO: can also overload [] to return a proxy for a position() iterator with
    // get(), set(), and del() methods.
    // -> position() functor might be private

};


/* NOTE: because the operator overloads are fully generic and defined outside the
mixin, we need to explicitly disable them for types where they are not valid, otherwise
the compiler will attempt to use them wherever a comparison is made anywhere in the
codebase, regardless of the actual types of the operands. */
template <typename Derived, typename ReturnType>
using enable_list_ops = std::enable_if_t<
    std::is_base_of_v<ListOps_<Derived>, Derived>, ReturnType
>;


/* Allow Python-style concatenation between Linked data structures and arbitrary
Python/C++ containers. */
template <typename T, typename Derived>
inline auto operator+(const Derived& lhs, const T& rhs)
    -> enable_list_ops<Derived, Derived>
{
    std::optional<Derived> result = lhs.copy();
    if (!result.has_value()) {
        throw std::runtime_error("could not copy list");
    }
    result.value().extend(rhs);  // must be specialized for T
    return Derived(std::move(result.value()));
}


/* Allow Python-style concatenation between list-like C++ containers and Linked data
structures. */
template <typename T, typename Derived>
inline auto operator+(const T& lhs, const Derived& rhs)
    -> std::enable_if_t<
        // NOTE: this checks whether the container T has a range-based insert() method
        // that we can use to insert the elements of rhs into lhs.  The return type of
        // the concatenation operator thus always matches that of the lhs operand.
        std::is_same_v<
            decltype(
                std::declval<T>().insert(
                    std::declval<T>().end(),
                    std::declval<Derived>().begin(),
                    std::declval<Derived>().end()
                )
            ),
            typename T::iterator
        >,
        enable_list_ops<Derived, T>
    >
{
    T result = lhs;
    result.insert(result.end(), rhs.begin(), rhs.end());  // STL compliant
    return result;
}


/* Allow Python-style concatenation between Python sequences and Linked data
structures. */
template <typename T, typename Derived>
inline auto operator+(const PyObject* lhs, const Derived& rhs)
    -> enable_list_ops<Derived, PyObject*>
{
    // Check that lhs is a Python sequence
    if (!PySequence_Check(lhs)) {
        std::ostringstream msg;
        msg << "can only concatenate sequence (not '";
        msg << lhs->ob_type->tp_name << "') to sequence";
        throw std::bad_typeid(msg.str());
    }

    // unpack list into Python sequence
    PyObject* seq = PySequence_List(rhs.iter.python());  // new ref
    if (seq == nullptr) {
        return nullptr;  // propagate error
    }

    // concatenate using Python API
    PyObject* concat = PySequence_Concat(lhs, seq);
    Py_DECREF(seq);
    return concat;
}


/* Allow in-place concatenation for Linked data structures using the += operator. */
template <typename T, typename Derived>
inline auto operator+=(Derived& lhs, const T& rhs)
    -> enable_list_ops<Derived, Derived&>
{
    lhs.extend(rhs);  // must be specialized for T
    return lhs;
}


// TODO: we could probably optimize repetition by allocating a block of nodes equal to
// list.size() * rhs.  We could also remove the extra copy in *= by using an iterator
// to the end of the list and reusing it for each iteration.


/* Allow Python-style repetition for Linked data structures using the * operator. */
template <typename = void, typename Derived>
auto operator*(const Derived& lhs, const ssize_t rhs)
    -> enable_list_ops<Derived, Derived>
{
    // handle empty repitition
    if (rhs <= 0 || lhs.size() == 0) {
        return Derived(lhs.max_size(), lhs.specialization());
    }

    // copy lhs
    std::optional<Derived> result = lhs.copy();
    if (!result.has_value()) {
        throw std::runtime_error("could not copy list");
    }

    // extend copy rhs - 1 times
    for (ssize_t i = 1; i < rhs; ++i) {
        result.value().extend(lhs);
    }

    // move result into return value
    return Derived(std::move(result.value()));
}


/* Allow Python-style repetition for Linked data structures using the * operator. */
template <typename = void, typename Derived>
inline auto operator*(const ssize_t lhs, const Derived& rhs)
    -> enable_list_ops<Derived, Derived>
{
    return rhs * lhs;  // symmetric
}


/* Allow Python-style repetition for Linked data structures using the * operator. */
template <typename = void, typename Derived>
auto operator*(const Derived& lhs, const PyObject* rhs)
    -> enable_list_ops<Derived, Derived>
{
    // Check that rhs is a Python integer
    if (!PyLong_Check(rhs)) {
        std::ostringstream msg;
        msg << "can't multiply sequence by non-int of type '";
        msg << rhs->ob_type->tp_name << "'";
        throw std::bad_typeid(msg.str());
    }

    // convert to C++ integer
    ssize_t val = PyLong_AsSsize_t(rhs);
    if (val == -1 && PyErr_Occurred()) {
        throw std::runtime_error("could not convert Python integer to C++ integer");
    }

    // delegate to C++ overload
    return lhs * val;
}


/* Allow Python-style repetition for Linked data structures using the * operator. */
template <typename = void, typename Derived>
inline auto operator*(const PyObject* lhs, const Derived& rhs)
    -> enable_list_ops<Derived, Derived>
{
    return rhs * lhs;  // symmetric
}


/* Allow in-place repetition for Linked data structures using the *= operator. */
template <typename = void, typename Derived>
auto operator*=(Derived& lhs, const ssize_t rhs)
    -> enable_list_ops<Derived, Derived&>
{
    // handle empty repitition
    if (rhs <= 0 || lhs.size() == 0) {
        lhs.clear();
        return lhs;
    }

    // copy lhs
    std::optional<Derived> copy = lhs.copy();
    if (!copy.has_value()) {
        throw std::runtime_error("could not copy list");
    }

    // extend lhs rhs - 1 times
    for (ssize_t i = 1; i < rhs; ++i) {
        lhs.extend(copy.value());
    }
    return lhs;
}


/* Allow in-place repetition for Linked data structures using the *= operator. */
template <typename = void, typename Derived>
inline auto operator*=(Derived& lhs, const PyObject* rhs)
    -> enable_list_ops<Derived, Derived&>
{
    // Check that rhs is a Python integer
    if (!PyLong_Check(rhs)) {
        std::ostringstream msg;
        msg << "can't multiply sequence by non-int of type '";
        msg << rhs->ob_type->tp_name << "'";
        throw std::bad_typeid(msg.str());
    }

    // convert to C++ integer
    ssize_t val = PyLong_AsSsize_t(rhs);
    if (val == -1 && PyErr_Occurred()) {
        throw std::runtime_error("could not convert Python integer to C++ integer");
    }

    // delegate to C++ overload
    return lhs *= val;
}


/* Allow lexicographic < comparison between Linked data structures and compatible C++
containers. */
template <typename T, typename Derived>
auto operator<(const Derived& lhs, const T& rhs)
    -> enable_list_ops<Derived, bool>
{
    // get coupled iterators
    auto iter_lhs = std::begin(lhs);
    auto end_lhs = std::end(lhs);
    auto iter_rhs = std::begin(rhs);
    auto end_rhs = std::end(rhs);

    // loop until one of the sequences is exhausted
    while (iter_lhs != end_lhs && iter_rhs != end_rhs) {
        if ((*iter_lhs)->value < *iter_rhs) return true;
        if (*iter_rhs < (*iter_lhs)->value) return false;
        ++iter_lhs;
        ++iter_rhs;
    }

    // check if lhs is shorter than rhs
    return (iter_lhs == end_lhs && iter_rhs != end_rhs);
}


/* Allow lexicographic < comparison between Linked data structures and Python
sequences. */
template <typename Derived>
auto operator<(const Derived& lhs, const PyObject* rhs)
    -> enable_list_ops<Derived, bool>
{
    // check that rhs is a Python sequence
    if (!PySequence_Check(rhs)) {
        std::ostringstream msg;
        msg << "can only compare list to sequence (not '";
        msg << rhs->ob_type->tp_name << "')";
        throw std::bad_typeid(msg.str());
    }

    // get coupled iterators
    auto iter_lhs = std::begin(lhs);
    auto end_lhs = std::end(lhs);
    PyIterable pyiter_rhs(rhs);  // handles reference counts
    auto iter_rhs = pyiter_rhs.begin();
    auto end_rhs = pyiter_rhs.end();

    // loop until one of the sequences is exhausted
    while (iter_lhs != end_lhs && iter_rhs != end_rhs) {
        // compare lhs < rhs
        int comp = PyObject_RichCompareBool((*iter_lhs)->value, *iter_rhs, Py_LT);
        if (comp == -1) {
            throw std::runtime_error("could not compare list elements");
        } else if (comp == 1) {
            return true;
        }

        // compare rhs < lhs
        comp = PyObject_RichCompareBool(*iter_rhs, (*iter_lhs)->value, Py_LT);
        if (comp == -1) {
            throw std::runtime_error("could not compare list elements");
        } else if (comp == 1) {
            return false;
        }

        // advance iterators
        ++iter_lhs;
        ++iter_rhs;
    }

    // check if lhs is shorter than rhs
    return (iter_lhs == end_lhs && iter_rhs != end_rhs);
}


/* Allow lexicographic < comparison between compatible C++ containers and Linked data
structures. */
template <typename T, typename Derived>
inline auto operator<(const T& lhs, const Derived& rhs)
    -> enable_list_ops<Derived, bool>
{
    return rhs > lhs;  // implies lhs < rhs
}


/* Allow lexicographic <= comparison between Linked data structures and compatible C++
containers. */
template <typename T, typename Derived>
auto operator<=(const Derived& lhs, const T& rhs)
    -> enable_list_ops<Derived, bool>
{
    // get coupled iterators
    auto iter_lhs = std::begin(lhs);
    auto end_lhs = std::end(lhs);
    auto iter_rhs = std::begin(rhs);
    auto end_rhs = std::end(rhs);

    // loop until one of the sequences is exhausted
    while (iter_lhs != end_lhs && iter_rhs != end_rhs) {
        if ((*iter_lhs)->value < *iter_rhs) return true;
        if (*iter_rhs < (*iter_lhs)->value) return false;
        ++iter_lhs;
        ++iter_rhs;
    }

    // check if lhs is exhausted
    return (iter_lhs == end_lhs);
}


/* Allow lexicographic <= comparison between Linked data structures and Python
sequences. */
template <typename Derived>
auto operator<=(const Derived& lhs, const PyObject* rhs)
    -> enable_list_ops<Derived, bool>
{
    // check that rhs is a Python sequence
    if (!PySequence_Check(rhs)) {
        std::ostringstream msg;
        msg << "can only compare list to sequence (not '";
        msg << rhs->ob_type->tp_name << "')";
        throw std::bad_typeid(msg.str());
    }

    // get coupled iterators
    auto iter_lhs = std::begin(lhs);
    auto end_lhs = std::end(lhs);
    PyIterable pyiter_rhs(rhs);  // handles reference counts
    auto iter_rhs = pyiter_rhs.begin();
    auto end_rhs = pyiter_rhs.end();

    // loop until one of the sequences is exhausted
    while (iter_lhs != end_lhs && iter_rhs != end_rhs) {
        // compare lhs < rhs
        int comp = PyObject_RichCompareBool((*iter_lhs)->value, *iter_rhs, Py_LT);
        if (comp == -1) {
            throw std::runtime_error("could not compare list elements");
        } else if (comp == 1) {
            return true;
        }

        // compare rhs < lhs
        comp = PyObject_RichCompareBool(*iter_rhs, (*iter_lhs)->value, Py_LT);
        if (comp == -1) {
            throw std::runtime_error("could not compare list elements");
        } else if (comp == 1) {
            return false;
        }

        // advance iterators
        ++iter_lhs;
        ++iter_rhs;
    }

    // check if lhs is exhausted
    return (iter_lhs == end_lhs);
}


/* Allow lexicographic <= comparison between compatible C++ containers and Linked data
structures. */
template <typename T, typename Derived>
inline auto operator<=(const T& lhs, const Derived& rhs)
    -> enable_list_ops<Derived, bool>
{
    return rhs >= lhs;  // implies lhs <= rhs
}


/* Allow == comparison between Linked data structures and compatible C++ containers. */
template <typename T, typename Derived>
auto operator==(const Derived& lhs, const T& rhs)
    -> enable_list_ops<Derived, bool>
{
    if (lhs.size() != rhs.size()) {
        return false;
    }

    // compare elements in order
    auto iter_rhs = std::begin(rhs);
    for (const auto& item : lhs) {
        if (item->value != *iter_rhs) {
            return false;
        }
        ++iter_rhs;
    }

    return true;
}


/* Allow == comparison betwen Linked data structures and Python sequences. */
template <typename Derived>
auto operator==(const Derived& lhs, const PyObject* rhs)
    -> enable_list_ops<Derived, bool>
{
    // check that rhs is a Python sequence
    if (!PySequence_Check(rhs)) {
        std::ostringstream msg;
        msg << "can only compare list to sequence (not '";
        msg << rhs->ob_type->tp_name << "')";
        throw std::bad_typeid(msg.str());
    }

    // check that lhs and rhs have the same length
    Py_ssize_t len = PySequence_Length(rhs);
    if (len == -1) {
        std::ostringstream msg;
        msg << "could not get length of sequence (of type '";
        msg << rhs->ob_type->tp_name << "')";
        throw std::bad_typeid(msg.str());
    } else if (lhs.size() != static_cast<size_t>(len)) {
        return false;
    }

    // compare elements in order
    PyIterable pyiter_rhs(rhs);  // handles reference counts
    auto iter_rhs = pyiter_rhs.begin();
    for (const auto& item : lhs) {
        int comp = PyObject_RichCompareBool(item->value, *iter_rhs, Py_EQ);
        if (comp == -1) {
            throw std::runtime_error("could not compare list elements");
        } else if (comp == 0) {
            return false;
        }
        ++iter_rhs;
    }

    return true;
}


/* Allow == comparison between compatible C++ containers and Linked data structures. */
template <typename T, typename Derived>
inline auto operator==(const T& lhs, const Derived& rhs)
    -> enable_list_ops<Derived, bool>
{
    return rhs == lhs;
}


/* Allow != comparison between Linked data structures and compatible C++ containers. */
template <typename T, typename Derived>
inline auto operator!=(const Derived& lhs, const T& rhs)
    -> enable_list_ops<Derived, bool>
{
    return !(lhs == rhs);
}


/* Allow != comparison between compatible C++ containers Linked data structures. */
template <typename T, typename Derived>
inline auto operator!=(const T& lhs, const Derived& rhs)
    -> enable_list_ops<Derived, bool>
{
    return !(lhs == rhs);
}


/* Allow lexicographic >= comparison between Linked data structures and compatible C++
containers. */
template <typename T, typename Derived>
inline auto operator>=(const Derived& lhs, const T& rhs)
    -> enable_list_ops<Derived, bool>
{
    return !(lhs < rhs);
}


/* Allow lexicographic >= comparison between compatible C++ containers and Linked data
structures. */
template <typename T, typename Derived>
inline auto operator>=(const T& lhs, const Derived& rhs)
    -> enable_list_ops<Derived, bool>
{
    return !(lhs < rhs);
}


/* Allow lexicographic > comparison between Linked data structures and compatible C++
containers. */
template <typename T, typename Derived>
inline auto operator>(const Derived& lhs, const T& rhs)
    -> enable_list_ops<Derived, bool>
{
    return !(lhs <= rhs);
}


/* Allow lexicographic > comparison between compatible C++ containers and Linked data
structures. */
template <typename T, typename Derived>
inline auto operator>(const T& lhs, const Derived& rhs)
    -> enable_list_ops<Derived, bool>
{
    return !(lhs <= rhs);
}


#endif  // BERTRAND_STRUCTS_LIST_LIST_H include guard
