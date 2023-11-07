// include guard: BERTRAND_STRUCTS_LINKED_ALGORITHMS_SLICE_H
#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_SLICE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_SLICE_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <sstream>  // std::ostringstream
#include <Python.h>  // CPython API
#include "../core/iter.h"  // Bidirectional<>
#include "../core/view.h"  // ViewTraits
#include "../../util/iter.h"  // iter()
#include "../../util/except.h"  // type_error()
#include "../../util/math.h"  // py_modulo()
#include "../../util/sequence.h"  // PySequence


namespace bertrand {
namespace structs {
namespace linked {


    /* Forward declarations. */
    template <typename View>
    class SliceIndices;
    template <typename List>
    class SliceProxy;


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
        long long default_start = step.value_or(0) < 0 ? size - 1 : 0;
        long long default_stop = step.value_or(0) < 0 ? -1 : size;
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
                start_ = step_ < 0 ? -1 : 0;
            }
        } else if (start_ >= size) {
            start_ = step_ < 0 ? size - 1 : size;
        }

        // normalize stop index
        long long stop_ = stop.value_or(default_stop);
        if (stop_ < 0) {
            stop_ += size;
            if (stop_ < 0) {
                stop_ = step_ < 0 ? -1 : 0;
            }
        } else if (stop_ > size) {
            stop_ = step_ < 0 ? size - 1 : size;
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
    auto slice(List& list, Args&&... args)
        -> std::enable_if_t<ViewTraits<typename List::View>::listlike, SliceProxy<List>>
    {
        return SliceProxy(
            list,
            normalize_slice(list.view, std::forward<Args>(args)...)
        );
    }


    /* A simple class representing the normalized indices needed to construct a slice
    from a linked data structure. */
    template <typename View>
    class SliceIndices {
    public:

        /* Slice indices. */
        long long start, stop, step;  // original indices supplied to constructor
        size_t abs_step;  // absolute value of step size
        size_t first, last;  // first and last indices included in slice
        size_t length;  // total number of items in slice
        bool inverted;  // check whether first and last indices confrom to step size
        bool backward;  // check whether slice is traversed from tail to head

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

        /* Copy assignment operator. */
        SliceIndices& operator=(const SliceIndices& other) {
            start = other.start;
            stop = other.stop;
            step = other.step;
            abs_step = other.abs_step;
            first = other.first;
            last = other.last;
            length = other.length;
            inverted = other.inverted;
            backward = other.backward;
            return *this;
        }

        /* Move assignment operator. */
        SliceIndices& operator=(SliceIndices&& other) {
            start = other.start;
            stop = other.stop;
            step = other.step;
            abs_step = other.abs_step;
            first = other.first;
            last = other.last;
            length = other.length;
            inverted = other.inverted;
            backward = other.backward;
            return *this;
        }

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
            long long start,
            long long stop,
            long long step,
            size_t length,
            size_t view_size
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
            if constexpr (NodeTraits<typename View::Node>::has_prev) {
                long long size = static_cast<long long>(view_size);
                bool cond = (
                    (step > 0 && start <= size - closed) ||
                    (step < 0 && size - start <= closed)
                );
                return cond ?
                    std::make_pair(start, closed) :
                    std::make_pair(closed, start);
            }

            // if singly-linked, always start from head of list
            return step > 0 ?
                std::make_pair(start, closed) :
                std::make_pair(closed, start);
        }

    };


    /* A proxy for a slice within a list, as returned by the slice() factory method. */
    template <typename List>
    class SliceProxy {
        using View = typename List::View;
        using Node = typename View::Node;
        using Value = typename View::Value;

        List& list;
        const SliceIndices<View> indices;
        mutable bool found;  // indicates whether we've cached the origin node
        mutable Node* _origin;  // node that immediately precedes slice (can be NULL)

        /* Find and cache the origin node for the slice. */
        Node* origin() const {
            if (found) return _origin;  // return cached origin

            // find origin node
            if constexpr (NodeTraits<Node>::has_prev) {
                if (backward()) {  // backward traversal
                    auto it = list.view.rbegin();
                    for (size_t i = 1; i < list.view.size() - first(); ++i) ++it;
                    found = true;
                    _origin = it.next();
                    return _origin;
                }
            }

            // forward traversal
            auto it = list.view.begin();
            for (size_t i = 0; i < first(); ++i) ++it;
            found = true;
            _origin = it.prev();
            return _origin;
        }

        /* Force use of slice() factory function. */
        template <typename _List, typename... Args>
        friend auto slice(_List& list, Args&&... args) -> std::enable_if_t<
            ViewTraits<typename _List::View>::listlike,
            SliceProxy<_List>
        >;

        /* Construct a SliceProxy with at least one element. */
        SliceProxy(List& list, SliceIndices<View>&& indices) :
            list(list), indices(indices), found(false), _origin(nullptr)
        {}

    public:

        /* Disallow SliceProxies from being stored as lvalues. */
        SliceProxy(const SliceProxy&) = delete;
        SliceProxy(SliceProxy&&) = delete;
        SliceProxy& operator=(const SliceProxy&) = delete;
        SliceProxy& operator=(SliceProxy&&) = delete;

        /* A specialized iterator built for slice traversal. */
        template <Direction dir>
        class Iterator : public View::template Iterator<dir> {
            using Base = typename View::template Iterator<dir>;
            friend SliceProxy;
            const SliceIndices<View>& indices;
            size_t idx;
            size_t skip;

            /* Get an iterator to the start of the slice. */
            Iterator(View& view, const SliceIndices<View>& indices, Node* origin) :
                Base(view), indices(indices), idx(0), skip(0)
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
            Iterator(View& view, const SliceIndices<View>& indices) :
                Base(view), indices(indices), idx(indices.length), skip(0)
            {}

        public:

            /* Copy constructor. */
            Iterator(const Iterator& other) noexcept :
                Base(other), indices(other.indices), idx(other.idx), skip(other.skip)
            {}

            /* Move constructor. */
            Iterator(Iterator&& other) noexcept :
                Base(std::move(other)), indices(std::move(other.indices)),
                idx(other.idx), skip(other.skip)
            {}

            /* Prefix increment to advance the iterator to the next node in the slice. */
            inline Iterator& operator++() noexcept {
                ++idx;
                if (idx == indices.length) {
                    return *this;  // don't advance past end of slice
                }

                if constexpr (dir == Direction::backward) {
                    for (size_t i = skip; i < indices.abs_step; ++i) {
                        this->_next = this->_curr;
                        this->_curr = this->_prev;
                        this->_prev = this->_curr->prev();
                    }
                } else {
                    for (size_t i = skip; i < indices.abs_step; ++i) {
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
                return idx != other.idx;
            }

            /* Get the current iteration step of the iterator. */
            inline size_t index() const noexcept {
                return idx;
            }

            /* Remove the node at the current position. */
            inline Node* drop() {
                ++skip;
                return Base::drop();
            }

        };

        /* Return an iterator to the start of the slice. */
        inline auto begin() const {
            // backward traversal
            if constexpr (NodeTraits<Node>::has_prev) {
                using Backward = Iterator<Direction::backward>;
                if (backward()) {
                    return Bidirectional(Backward(list.view, indices, origin()));
                }
            }

            // forward traversal
            using Forward = Iterator<Direction::forward>;
            return Bidirectional(Forward(list.view, indices, origin()));        
        }

        /* Return an iterator to the end of the slice. */
        inline auto end() const {
            // backward traversal
            if constexpr (NodeTraits<Node>::has_prev) {
                using Backward = Iterator<Direction::backward>;
                if (backward()) {
                    return Bidirectional(Backward(list.view, indices));
                }
            }

            // forward traversal
            using Forward = Iterator<Direction::forward>;
            return Bidirectional(Forward(list.view, indices));
        }

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
            // NOTE: if original list has fixed size, then so will the slice.  We just
            // have to adjust the max_size parameter to use the smaller slice length
            // rather than the original list size.
            std::optional<size_t> max_size = list.dynamic() ?
                std::nullopt :
                std::make_optional(length());
            List result(max_size, list.specialization());

            // trivial case: empty slice
            if (empty()) return result;

            // preallocate memory for nodes
            typename View::MemGuard guard = result.reserve(length());

            // copy nodes from original view into result
            for (auto it = this->begin(), end = this->end(); it != end; ++it) {
                Node* copy = result.view.node(*(it.curr()));
                if (inverted()) {  // correct for inverted traversal
                    result.view.link(nullptr, copy, result.view.head());
                } else {
                    result.view.link(result.view.tail(), copy, nullptr);
                }
            }
            return result;
        }

        /* Replace a slice within a linked list. */
        template <typename Container>
        void set(const Container& items) {
            // unpack iterable into temporary sequence (unless it is already a sequence)
            auto sequence = util::sequence(items);

            // trvial case: both slice and sequence are empty
            if (empty() && sequence.size() == 0) return;

            // check slice length matches sequence length
            if (step() != 1 && length() != sequence.size()) {
                std::ostringstream msg;
                msg << "attempt to assign sequence of size " << sequence.size();
                msg << " to extended slice of size " << length();
                throw std::invalid_argument(msg.str());
            }

            // temporary array to undo changes in case of error
            struct RecoveryArray {
                Node* array;
                RecoveryArray(size_t length) {
                    array = static_cast<Node*>(malloc(sizeof(Node) * length));
                    if (array == nullptr) throw std::bad_alloc();
                }
                Node& operator[](size_t index) { return array[index]; }
                ~RecoveryArray() { free(array); }  // does not call destructors
            };

            // allocate recovery array
            RecoveryArray recovery(length());

            // hold allocator at current size (or grow if performing a slice insertion)
            typename View::MemGuard guard = 
                list.reserve(list.size() + sequence.size() - length());

            // loop 1: remove current nodes in slice
            for (auto it = this->begin(), end = this->end(); it != end; ++it) {
                Node* node = it.drop();
                new (&recovery[it.index()]) Node(std::move(*node));  // move to recovery
                list.view.recycle(node);  // recycle original node
            }

            // loop 2: insert new nodes from sequence into vacated slice
            size_t idx = 0;
            try {
                // NOTE: we iterate over the sequence, not the slice, in order to allow
                // for Python-style slice insertions (e.g. slice[1:2] = [1, 2, 3]).  In
                // these cases, Python allows the slice and sequence lengths to differ,
                // and continues inserting items until the sequence is exhausted.
                auto iter = util::iter(sequence);
                auto seq = inverted() ? iter.reverse() : iter.forward();
                for (auto it = this->begin(); seq != seq.end(); ++it, ++seq, ++idx) {
                    it.insert(list.view.node(*seq));
                }

            // rewind if an error occurs
            } catch (...) {
                // NOTE: we can use the recovery array to restore the original list in
                // the event of error.  This is an extremely delicate process, as we
                // must ensure that all existing nodes are cleaned up, no matter where
                // the error occurred.  We must also avoid memory leaks, and ensure
                // that all nodes are properly recycled and destroyed.

                // loop 3: remove nodes that have already been added to list
                size_t i = 0;
                for (auto it = this->begin(); i < idx; ++i, ++it) {
                    list.view.recycle(it.drop());
                }

                // loop 4: reinsert original nodes from recovery array
                for (auto it = this->begin(), end = this->end(); it != end; ++it) {
                    Node& recovery_node = recovery[it.index()];
                    it.insert(list.view.node(std::move(recovery_node)));
                    recovery_node.~Node();  // destroy recovery node
                }
                throw;  // propagate
            }

            // loop 3: deallocate removed nodes
            for (size_t i = 0; i < length(); i++) {
                recovery[i].~Node();  // release recovery node
            }
        }

        /* Delete a slice within a linked list. */
        void del() {
            // trivial case: slice is empty
            if (empty()) return;

            // hold allocator at current size until all nodes are removed
            typename View::MemGuard guard = list.reserve();

            // recycle every node in slice
            for (auto it = this->begin(), end = this->end(); it != end; ++it) {
                list.view.recycle(it.drop());
            }
        }

    };


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_SLICE_H
