// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_ALGORITHMS_SLICE_H
#define BERTRAND_STRUCTS_ALGORITHMS_SLICE_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <sstream>  // std::ostringstream
#include <Python.h>  // CPython API
#include "../iter.h"  // Bidirectional<>
#include "../../util/iter.h"  // CoupledIterator<>
#include "../../util/except.h"  // type_error()
#include "../../util/math.h"  // py_modulo()
#include "../../util/python.h"  // PySequence


namespace bertrand {
namespace structs {
namespace linked {
namespace algorithms {


namespace list {

    template <typename View>
    class SliceIndices;  // forward declaration

    template <typename List>
    class SliceProxy;  // forward declaration

    /* Normalize slice indices, applying Python-style wraparound and bounds
    checking. */
    template <typename View>
    SliceIndices<View> normalize_slice(
        View& view,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt,
        std::optional<long long> step = std::nullopt
    ) {
        // normalize slice indices
        long long size = static_cast<long long>(view.size());
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
        return SliceIndices<View>(start_, stop_, step_, length, size);
    }

    /* Normalize a Python slice object, applying Python-style wraparound and bounds
    checking. */
    template <typename View>
    SliceIndices<View> normalize_slice(View& view, PyObject* py_slice) {
        // check that input is a Python slice object
        if (!PySlice_Check(py_slice)) {
            throw util::type_error("index must be a Python slice");
        }

        size_t size = view.size();

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
        return SliceIndices<View>(start, stop, step, length, size);
    }


    /* Get a proxy for a slice within the list. */
    template <typename List, typename... Args>
    SliceProxy<List> slice(List& list, Args&&... args) {
        return SliceProxy(
            list, normalize_slice(list.view, std::forward<Args>(args)...)
        );
    }

    /* A simple class representing the normalized indices needed to construct a slice
    from a linked data structure. */
    template <typename View>
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
        template <typename _View>
        friend SliceIndices<_View> normalize_slice(
            _View& view,
            std::optional<long long> start,
            std::optional<long long> stop,
            std::optional<long long> step
        );

        template <typename _View>
        friend SliceIndices<_View> normalize_slice(_View& view, PyObject* py_slice);

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
            long long mod = util::py_modulo((stop - start), step);
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

        /* Swap the start and stop indices based on singly-/doubly-linked status. */
        std::pair<long long, long long> slice_direction(
            long long closed,
            size_t view_size
        ) {
            // if doubly-linked, start at whichever end is closest to slice boundary
            if constexpr (View::Node::doubly_linked) {
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

    /* A proxy for a slice within a list, as returned by the slice() factory method. */
    template <typename List>
    class SliceProxy {
        using View = typename List::View;
        using Node = typename View::Node;
        using Value = typename View::Value;

        template <Direction dir>
        using ViewIterator = typename View::template Iterator<dir>;

    public:

        /* A specialized iterator built for slice traversal. */
        template <Direction dir>
        class Iterator : public ViewIterator<dir> {
            using Base = ViewIterator<dir>;

        public:

            /* Dereference to get the value at the current node. */
            inline Value operator*() const noexcept {
                return this->_curr->value();
            }

            /* Prefix increment to advance the iterator to the next node in the slice. */
            inline Iterator& operator++() noexcept {
                ++this->_idx;
                if (this->_idx == length_override) {
                    return *this;  // don't jump on last iteration
                }

                if constexpr (dir == Direction::backward) {
                    for (size_t i = implicit_skip; i < indices.abs_step; ++i) {
                        this->_next = this->_curr;
                        this->_curr = this->_prev;
                        this->_prev = this->_curr->prev();
                    }
                } else {
                    for (size_t i = implicit_skip; i < indices.abs_step; ++i) {
                        this->_prev = this->_curr;
                        this->_curr = this->_next;
                        this->_next = this->_curr->next();
                    }
                }
                return *this;
            }

            /* Inequality comparison to terminate the slice. */
            template <Direction T>
            inline bool operator!=(const Iterator<T>& other) const noexcept {
                return _idx != other._idx;
            }

            //////////////////////////////
            ////    HELPER METHODS    ////
            //////////////////////////////

            /* Get the current index of the iterator within the list. */
            inline size_t index() const noexcept {
                if constexpr (dir == Direction::backward) {
                    return indices.first - (indices.abs_step * _idx);
                } else {
                    return indices.first + (indices.abs_step * _idx);
                }
            }

            /* Get the current iteration step of the iterator. */
            inline size_t idx() const noexcept {
                return _idx;
            }

            /* Remove the node at the current position. */
            inline Node* remove() {
                ++implicit_skip;
                return Base::remove();
            }

            /* Copy constructor. */
            Iterator(const Iterator& other) noexcept :
                Base(other), indices(other.indices), _idx(other._idx),
                length_override(other.length_override),
                implicit_skip(other.implicit_skip)
            {}

            /* Move constructor. */
            Iterator(Iterator&& other) noexcept :
                Base(std::move(other)), indices(std::move(other.indices)),
                _idx(other._idx), length_override(other.length_override),
                implicit_skip(other.implicit_skip)
            {}

        protected:
            friend SliceProxy;
            const SliceIndices<View>& indices;
            size_t _idx;
            size_t length_override;
            size_t implicit_skip;

            ////////////////////////////
            ////    CONSTRUCTORS    ////
            ////////////////////////////

            /* Get an iterator to the start of the slice. */
            Iterator(
                View& view,
                Node* origin,
                const SliceIndices<View>& indices,
                size_t length_override
            ) : Base(view), indices(indices), _idx(0), length_override(length_override),
                implicit_skip(0)
            {
                if constexpr (dir == Direction::backward) {
                    this->_next = origin;
                    if (this->_next == nullptr) {
                        this->_curr = this->view.tail();
                    } else {
                        this->_curr = this->_next->prev();
                    }
                    if (this->_curr != nullptr) {
                        this->_prev = this->_curr->prev();
                    }
                } else {
                    this->_prev = origin;
                    if (this->_prev == nullptr) {
                        this->_curr = this->view.head();
                    } else {
                        this->_curr = this->_prev->next();
                    }
                    if (this->_curr != nullptr) {
                        this->_next = this->_curr->next();
                    }
                }
            }

            /* Get an empty iterator to terminate the slice. */
            Iterator(
                View& view,
                const SliceIndices<View>& indices,
                size_t length_override
            ) : Base(view), indices(indices), _idx(length_override),
                length_override(length_override), implicit_skip(0)
            {}

        };

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

        /* Extract a slice from a linked list. */
        List get() const {
            // TODO: might be able to allocate a list directly and then just access its
            // view to do the linking.   Or append directly to the list.

            // allocate a new list to hold the slice
            std::optional<size_t> max_size = list.max_size(); 
            if (max_size.has_value()) {
                max_size = static_cast<size_t>(length());  // adjust to slice length
            }
            View result(max_size, list.specialization());

            // if slice is empty, return empty view
            if (empty()) {
                return List(std::move(result));
            }

            // copy nodes from original view into result
            result.reserve(length());  // preallocate for efficiency
            for (auto iter = this->iter(); iter != iter.end(); ++iter) {
                Node* copy = result.node(*(iter.curr()));
                if (inverted()) {
                    result.link(nullptr, copy, result.head());
                } else {
                    result.link(result.tail(), copy, nullptr);
                }
            }
            return List(std::move(result));
        }

        // TODO:
        // l = LinkedList("abcdef")
        // l[2:2] = "xyz"
        // LinkedList(['a', 'b', 'x', 'y', 'c', 'd', 'e', 'f'])  // missing 'z'

        /* Replace a slice within a linked list. */
        void set(PyObject* items) {
            // unpack iterable into reversible sequence
            util::PySequence sequence(items, "can only assign an iterable");

            // trvial case: both slice and sequence are empty
            if (empty() && sequence.size() == 0) {
                return;
            }

            // check slice length matches sequence length
            if (length() != sequence.size() && step() != 1) {
                // NOTE: Python allows forced insertion if and only if the step size is 1
                std::ostringstream msg;
                msg << "attempt to assign sequence of size " << sequence.size();
                msg << " to extended slice of size " << length();
                throw std::invalid_argument(msg.str());
            }

            // temporary array to undo changes in case of error
            struct RecoveryArray {
                Node* nodes;  // Node[length()]
                RecoveryArray(size_t length) {
                    nodes = static_cast<Node*>(malloc(sizeof(Node) * length));
                    if (nodes == nullptr) {
                        throw std::bad_alloc();
                    }
                }
                ~RecoveryArray() { free(nodes); }
                Node& operator[](size_t index) { return nodes[index]; }
            };

            // allocate recovery array
            RecoveryArray recovery(length());

            // loop 1: remove current nodes in slice
            for (auto iter = this->iter(); iter != iter.end(); ++iter) {
                Node* node = iter.remove();  // remove node from list
                new (&recovery[iter.idx()]) Node(*node);  // copy into recovery array
                list.view.recycle(node);  // recycle original node
            }

            // loop 2: insert new nodes from sequence into vacated slice
            for (auto iter = this->iter(sequence.size()); iter != iter.end(); ++iter) {
                PyObject* item;
                if (inverted()) {  // count from back
                    item = sequence[sequence.size() - 1 - iter.idx()];
                } else {  // count from front
                    item = sequence[iter.idx()];
                }

                // allocate a new node for the list
                try {
                    Node* node = list.view.node(item);
                    iter.insert(node);

                // rewind if an error occurs
                } catch (...) {
                    // loop 3: remove nodes that have already been added to list
                    for (auto it = this->iter(iter.idx()); it != it.end(); ++it) {
                        Node* node = it.remove();
                        list.view.recycle(node);
                    }

                    // loop 4: reinsert original nodes from recovery array
                    for (auto it = this->iter(); it != it.end(); ++it) {
                        Node* recovery_node = &recovery[it.idx()];
                        it.insert(list.view.node(*recovery_node));  // copy into list
                        recovery_node->~Node();  // destroy recovery node
                    }
                    throw;  // propagate
                }
            }

            // loop 3: deallocate removed nodes
            for (size_t i = 0; i < length(); i++) {
                (&recovery[i])->~Node();  // release recovery node
            }
        }

        /* Delete a slice within a linked list. */
        void del() {
            // trivial case: slice is empty
            if (empty()) {
                return;
            }

            // recycle every node in slice
            for (auto iter = this->iter(); iter != iter.end(); ++iter) {
                Node* node = iter.remove();  // remove from list
                list.view.recycle(node);
            }
        }

        // TODO: SliceProxy might be better off outside the class and templated on the
        // variant.  It could also have a full battery of iterators.  The reverse
        // iterators would all use a stack to traverse the list in reverse order.

        // list.slice().iter()
        // list.slice().iter.reverse()
        // list.slice().iter.begin()
        // list.slice().iter.end()
        // list.slice().iter.rbegin()
        // list.slice().iter.rend()
        // list.slice().iter.python()
        // list.slice().iter.rpython()

        /* Return a coupled pair of iterators with a possible length override. */
        inline auto iter(std::optional<size_t> length = std::nullopt) const {
            using Forward = Iterator<Direction::forward>;

            // default to length of slice
            if (!length.has_value()) {
                return util::CoupledIterator<Bidirectional<Iterator>>(begin(), end());  
            }

            // use length override if given
            size_t len = length.value();

            // backward traversal
            if constexpr (Node::doubly_linked) {
                using Backward = Iterator<Direction::backward>;
                if (backward()) {
                    return util::CoupledIterator<Bidirectional<Iterator>>(
                        Bidirectional(Backward(list.view, origin(), indices, len)),
                        Bidirectional(Backward(list.view, indices, len))
                    );
                }
            }

            // forward traversal
            return util::CoupledIterator<Bidirectional<Iterator>>(
                Bidirectional(Forward(list.view, origin(), indices, len)),
                Bidirectional(Forward(list.view, indices, len))
            );
        }

        /* Return an iterator to the start of the slice. */
        inline auto begin() const {
            using Forward = Iterator<Direction::forward>;

            // account for empty sequence
            if (empty()) {
                return Bidirectional(Forward(list.view, indices, length()));
            }

            // backward traversal
            if constexpr (Node::doubly_linked) {
                using Backward = Iterator<Direction::backward>;
                if (backward()) {
                    return Bidirectional(Backward(list.view, origin(), indices, length()));
                }
            }

            // forward traversal
            return Bidirectional(Forward(list.view, origin(), indices, length()));        
        }

        /* Return an iterator to the end of the slice. */
        inline auto end() const {
            using Forward = Iterator<Direction::forward>;

            // return same orientation as begin()
            if (empty()) {
                return Bidirectional(Forward(list.view, indices, length()));
            }

            // backward traversal
            if constexpr (Node::doubly_linked) {
                using Backward = Iterator<Direction::backward>;
                if (backward()) {
                    return Bidirectional(Backward(list.view, indices, length()));
                }
            }

            // forward traversal
            return Bidirectional(Forward(list.view, indices, length()));
        }

    private:
        template <typename _List, typename... Args>
        friend SliceProxy<_List> slice(_List& list, Args&&... args);

        List& list;
        const SliceIndices<View> indices;
        mutable bool found;  // indicates whether we've cached the origin node
        mutable Node* _origin;  // node that immediately precedes slice (can be NULL)

        /* Construct a SliceProxy with at least one element. */
        SliceProxy(List& list, SliceIndices<View>&& indices) :
            list(list), indices(indices), found(false), _origin(nullptr)
        {}

        /* Find and cache the origin node for the slice. */
        Node* origin() const {
            if (found) {
                return _origin;
            }

            // find origin node
            if constexpr (Node::doubly_linked) {
                if (backward()) {  // backward traversal
                    Node* next = nullptr;
                    Node* curr = list.view.tail();
                    for (size_t i = list.view.size() - 1; i > first(); i--) {
                        next = curr;
                        curr = curr->prev();
                    }
                    found = true;
                    _origin = next;
                    return _origin;
                }
            }

            // forward traversal
            Node* prev = nullptr;
            Node* curr = list.view.head();
            for (size_t i = 0; i < first(); i++) {
                prev = curr;
                curr = curr->next();
            }
            found = true;
            _origin = prev;
            return _origin;
        }

    };


}  // namespace list


}  // namespace algorithms
}  // namespace linked
}  // namespace structs
}  // namespace bertrand



#endif // BERTRAND_STRUCTS_ALGORITHMS_SLICE_H include guard
