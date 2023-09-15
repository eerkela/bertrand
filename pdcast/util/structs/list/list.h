// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_LIST_LIST_H
#define BERTRAND_STRUCTS_LIST_LIST_H

// TODO: additional includes as necessary

#include "core/view.h"
#include "core/sort.h"

#include <list>  // std::list
#include <sstream>  // std::ostringstream
#include <typeinfo>  // std::bad_typeid
#include <vector>  // std::vector


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

    /* Method functor to create coupled iterators over the list. */
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

    /* Construct an empty list. */
    LinkedBase(long long max_size = -1, PyObject* spec = nullptr) :
        view(max_size, spec), iter(view)
    {}

    /* Construct a list from an input iterable. */
    LinkedBase(
        PyObject* iterable,
        bool reverse = false,
        long long max_size = -1,  // TODO: std::optional<size_t>?
        PyObject* spec = nullptr
    ) : view(iterable, reverse, max_size, spec), iter(view)
    {}

    /* Construct a list from a base view. */
    LinkedBase(View& view) : view(view), iter(view) {}

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


//////////////////////
////    MIXINS    ////
//////////////////////


/* A mixin that implements the full Python list interface. */
template <typename Derived, typename ViewType, typename SortPolicy>
class ListInterface_ {
    using View = ViewType;
    using Node = typename View::Node;
    using Vector = std::vector<PyObject*>;
    using List = std::list<PyObject*>;

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
        View& view = self().view;

        // get iterator to index
        auto iter = view.position(index);
        if (!iter.has_value()) {
            return;  // propagate error
        }

        // allocate a new node
        Node* node = view.node(item);
        if (node == nullptr) {
            return;  // propagate error
        }

        // attempt to insert
        iter.value().insert(node);
        if (PyErr_Occurred()) {
            view.recycle(node);  // clean up staged node before propagating
        }
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
    std::optional<size_t> index(PyObject* item, T start = 0, T stop = -1) const {
        const View& view = self().view;

        // normalize start/stop indices
        std::optional<size_t> opt_start = view.position.normalize(start, true);
        std::optional<size_t> opt_stop = view.position.normalize(stop, true);
        if (!opt_start.has_value() || !opt_stop.has_value()) {
            return std::nullopt;  // propagate error
        }
        size_t norm_start = opt_start.value();
        size_t norm_stop = opt_stop.value();
        if (norm_start > norm_stop) {
            PyErr_Format(
                PyExc_ValueError,
                "start index cannot be greater than stop index"
            );
            return std::nullopt;
        }

        // if list is doubly-linked and stop is closer to tail than start is to head,
        // then we iterate backward from the tail
        if constexpr (view.doubly_linked) {
            if ((view.size - 1 - norm_stop) < norm_start) {
                // get backwards iterator to stop index
                auto iter = view.iter.reverse();
                size_t idx = view.size - 1;
                while (idx > norm_stop) {
                    ++iter;
                    --idx;
                }

                // search until we hit start index
                bool found = false;
                size_t last_observed;
                while (idx >= norm_start) {
                    int comp = PyObject_RichCompareBool((*iter)->value, item, Py_EQ);
                    if (comp == -1) {
                        return std::nullopt;  // propagate error
                    } else if (comp == 1) {
                        found = true;
                        last_observed = idx;
                    }
                    ++iter;
                    --idx;
                }
                if (found) {
                    return std::make_optional(last_observed);
                }
                PyErr_Format(PyExc_ValueError, "%R is not in list", item);
                return std::nullopt;
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
        while (idx < norm_stop) {
            int comp = PyObject_RichCompareBool((*iter)->value, item, Py_EQ);
            if (comp == -1) {
                return std::nullopt;  // propagate error
            } else if (comp == 1) {
                return std::make_optional(idx);
            }
            ++iter;
            ++idx;
        }
        PyErr_Format(PyExc_ValueError, "%R is not in list", item);
        return std::nullopt;
    }

    /* Count the number of occurrences of an item within a list. */
    template <typename T>
    std::optional<size_t> count(PyObject* item, T start = 0, T stop = -1) const {
        const View& view = self().view;

        // normalize start/stop indices
        std::optional<size_t> opt_start = view.position.normalize(start, true);
        std::optional<size_t> opt_stop = view.position.normalize(stop, true);
        if (!opt_start.has_value() || !opt_stop.has_value()) {
            return std::nullopt;  // propagate error
        }
        size_t norm_start = opt_start.value();
        size_t norm_stop = opt_stop.value();
        if (norm_start > norm_stop) {
            PyErr_Format(
                PyExc_ValueError,
                "start index cannot be greater than stop index"
            );
            return std::nullopt;
        }

        // if list is doubly-linked and stop is closer to tail than start is to head,
        // then we iterate backward from the tail
        if constexpr (view.doubly_linked) {
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
                        return std::nullopt;  // propagate error
                    } else if (comp == 1) {
                        ++count;
                    }
                    ++iter;
                    --idx;
                }
                return std::make_optional(count);
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
                return std::nullopt;  // propagate error
            } else if (comp == 1) {
                ++count;
            }
            ++iter;
            ++idx;
        }
        return std::make_optional(count);
    }

    /* Check if the list contains a certain item. */
    std::optional<bool> contains(PyObject* item) const {
        const View& view = self().view;

        for (auto node : view) {
            // C API equivalent of the == operator
            int comp = PyObject_RichCompareBool(node->value, item, Py_EQ);
            if (comp == -1) {  // == comparison raised an exception
                return std::nullopt;
            } else if (comp == 1) {  // found a match
                return std::make_optional(true);
            }
        }

        // item not found
        return std::make_optional(false);
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
        View& view = self().view;

        // get an iterator to the specified index
        auto iter = view.position(index);
        if (!iter.has_value()) {
            return nullptr;  // propagate error
        }

        // remove node at index and return its value
        Node* node = iter.value().remove();
        PyObject* result = node->value;
        Py_INCREF(result);  // ensure value is not garbage collected during recycle()
        view.recycle(node);
        return result;
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
        
        if constexpr (view.doubly_linked) {
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
        if constexpr (view.doubly_linked) {
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

    ////////////////////////
    ////    FUNCTORS    ////
    ////////////////////////

    /* A functor that generates iterators to a specific index of the list. */
    const IndexFactory<View> position;
    /* position(T index, bool truncate = false)
     * position.forward(T index, bool truncate = false)
     * position.backward(T index, bool truncate = false)
     * position.normalize(T index, bool truncate = false)
     */

    /* A functor that generates slice proxies for elements within a list. */
    const SliceFactory<View> slice;
    /* slice(PyObject* slice)
     * slice(long long start = *, long long stop = *, long long step = *)
     * slice.normalize(PyObject* slice)
     * slice.normalize(long long start = *, long long stop = *, long long step = *)
     */

    /* A functor that sorts the list in place. */
    const SortPolicy sort;
    /* sort(PyObject* key = nullptr, bool reverse = false)
     */

protected:

    ListInterface_(View& view) :
        position(view), slice(view), sort(view)
    {}

private:

    inline Derived& self() {
        return static_cast<Derived&>(*this);
    }

    inline const Derived& self() const {
        return static_cast<const Derived&>(*this);
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
    LinkedList(View& view) :
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


#endif  // BERTRAND_STRUCTS_LIST_LIST_H include guard
