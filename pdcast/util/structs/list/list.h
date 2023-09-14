// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_LIST_LIST_H
#define BERTRAND_STRUCTS_LIST_LIST_H

// TODO: additional includes as necessary

#include "core/view.h"
#include "core/sort.h"


/*
list_cython.h
list.h
list.pxd
list.pyi
list.pyx
*/


//////////////////////
////    MIXINS    ////
//////////////////////


/* Base class that forwards the public members of the underlying view. */
template <typename View>
class LinkedBase {
public:
    inline static constexpr bool doubly_linked = View::doubly_linked;

    /* Every LinkedList contains a view that manages low-level node
    allocation/deallocation and links between nodes. */
    View view;

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

        /* Invoke the functor to get a coupled iterator over the list. */
        inline auto operator()() const {
            return view.iter();
        }

        /* Get a coupled reverse iterator over the list. */
        inline auto reverse() const {
            return view.iter.reverse();
        }

        /* Get a forward iterator to the head of the list. */
        inline auto begin() const {
            return view.iter.begin();
        }

        /* Get an empty forward iterator to terminate the sequence. */
        inline auto end() const {
            return view.iter.end();
        }

        /* Get a backward iterator to the tail of the list. */
        template <typename = std::enable_if<doubly_linked>>
        inline auto rbegin() const {
            return view.iter.rbegin();
        }

        /* Get an empty backward iterator to terminate the sequence. */
        template <typename = std::enable_if<doubly_linked>>
        inline auto rend() const {
            return view.iter.rend();
        }

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
        long long max_size = -1,
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


/* A mixin that implements the full Python list interface. */
template <typename Derived, typename View, typename SortPolicy>
class ListInterface_ {
    using Node = typename View::Node;  // compiler error if declared outside method

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

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////

    // TODO: overload +, +=, *, *=, ==, !=, <, <=, >, >=, etc.

    // TODO: can also overload [] to return a proxy for a position() iterator with
    // get(), set(), and del() methods.
    // -> position() functor might be private

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


// TODO: ListOps?
// -> operator overloads for list concatenation, repetition, comparison, etc.


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
