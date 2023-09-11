// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_SLICE_H
#define BERTRAND_STRUCTS_CORE_SLICE_H

#include <optional>  // std::optional
#include <type_traits>  // std::enable_if_t
#include <Python.h>  // CPython API
#include "node.h"  // has_prev<>
#include "index.h"  // IndexFactory
#include "util.h"  // CoupledIterator, Bidirectional


////////////////////////////////////
////    FORWARD DECLARATIONS    ////
////////////////////////////////////


template <typename ViewType>
class SliceProxy;


////////////////////////
////    FUNCTORS    ////
////////////////////////


/*
NOTE: SliceFactory is a functor (function object) that produces proxies for a linked
list, set, or dictionary that refer to slices of the original view.  It is used by the
view classes to allow for intuitive slicing with the same semantics as Python's
built-in list type, including default values and negative indices.
*/


/* A functor that constructs bidirectional proxies for slices within the templated
view. */
template <typename ViewType>
class SliceFactory {
public:
    using View = ViewType;
    using Node = typename View::Node;
    class Indices;
    using Slice = SliceProxy<View>;
    inline static constexpr bool doubly_linked = has_prev<Node>::value;

    /* Return a Slice proxy over the given indices. */
    template <typename... Args>
    std::optional<Slice> operator()(Args... args) const {
        auto indices = normalize(args...);
        if (!indices.has_value()) {
            return std::nullopt;
        }
        return std::make_optional(Slice(view, std::move(indices.value())));
    }

    /////////////////////////////////
    ////    NAMESPACE METHODS    ////
    /////////////////////////////////

    /* Normalize slice indices, applying Python-style wraparound and bounds
    checking. */
    std::optional<Indices> normalize(
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt,
        std::optional<long long> step = std::nullopt
    ) const {
        // normalize slice indices
        long long size = static_cast<long long>(view.size);
        long long default_start = (step.value_or(0) < 0) ? (size - 1) : (0);
        long long default_stop = (step.value_or(0) < 0) ? (-1) : (size);
        long long default_step = 1;

        // normalize step
        long long step_ = step.value_or(default_step);
        if (step_ == 0) {
            PyErr_SetString(PyExc_ValueError, "slice step cannot be zero");
            return std::nullopt;
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

        // return as Indices
        return std::make_optional(Indices(start_, stop_, step_, length, view.size));
    }

    /* Normalize a Python slice object, applying Python-style wraparound and bounds
    checking. */
    std::optional<Indices> normalize(PyObject* py_slice) const {
        // check that input is a Python slice object
        if (!PySlice_Check(py_slice)) {
            PyErr_SetString(PyExc_TypeError, "index must be a Python slice");
            return std::nullopt;
        }

        // use CPython API to get slice indices
        Py_ssize_t py_start, py_stop, py_step, py_length;
        int err = PySlice_GetIndicesEx(
            py_slice, view.size, &py_start, &py_stop, &py_step, &py_length
        );
        if (err == -1) {
            return std::nullopt;  // propagate error
        }

        // cast from Py_ssize_t
        long long start = static_cast<long long>(py_start);
        long long stop = static_cast<long long>(py_stop);
        long long step = static_cast<long long>(py_step);
        size_t length = static_cast<size_t>(py_length);

        // return as Indices
        return std::make_optional(Indices(start, stop, step, length, view.size));
    }

    /* A simple class representing the normalized indices needed to construct a
    coherent slice. */
    class Indices {
    public:
        /* Get the original indices that were supplied to the constructor. */
        inline long long start() const { return _start; }
        inline long long stop() const { return _stop; }
        inline long long step() const { return _step; }
        inline size_t abs_step() const { return _abs_step; }

        /* Get the first and last included indices. */
        inline size_t first() const { return _first; }
        inline size_t last() const { return _last; }

        /* Get the number of items included in the slice. */
        inline size_t length() const { return _length; }
        inline bool empty() const { return _length == 0; }

        /* Check if the first and last indices conform to the expected step size. */
        inline bool inverted() const { return _inverted; }
        inline bool backward() const { return _backward; }

        /* Copy constructor. */
        Indices(const Indices& other) :
            _start(other._start), _stop(other._stop), _step(other._step),
            _abs_step(other._abs_step), _first(other._first), _last(other._last),
            _length(other._length), _inverted(other._inverted),
            _backward(other._backward)
        {}

        /* Move constructor. */
        Indices(Indices&& other) :
            _start(other._start), _stop(other._stop), _step(other._step),
            _abs_step(other._abs_step), _first(other._first), _last(other._last),
            _length(other._length), _inverted(other._inverted),
            _backward(other._backward)
        {}

        /* Assignment operators deleted due to presence of const members. */
        Indices& operator=(const Indices& other) = delete;
        Indices& operator=(Indices&& other) = delete;

    private:
        friend SliceFactory;
        const long long _start;
        const long long _stop;
        const long long _step;
        const size_t _abs_step;
        size_t _first;
        size_t _last;
        const size_t _length;
        bool _inverted;
        bool _backward;

        Indices(
            const long long start,
            const long long stop,
            const long long step,
            const size_t length,
            const size_t view_size
        ) : _start(start), _stop(stop), _step(step), _abs_step(llabs(step)),
            _first(0), _last(0), _length(length), _inverted(false), _backward(false)
        {
            // convert to closed interval [start, closed]
            long long mod = py_modulo((stop - start), step);
            long long closed = (mod == 0) ? (stop - step) : (stop - mod);

            // get direction to traverse slice based on singly-/doubly-linked status
            std::pair<size_t, size_t> dir = slice_direction(closed, view_size);
            _first = dir.first;
            _last = dir.second;

            // Because we've adjusted our indices to minimize total iterations, we might
            // not be iterating in the same direction as the step size would indicate.
            // We must account for this when getting/setting items in the slice.
            _backward = (_first > ((view_size - (view_size > 0)) / 2));
            _inverted = _backward ^ (_step < 0);
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
        auto slice_direction(long long closed, size_t view_size) {
            // if doubly-linked, start at whichever end is closest to slice boundary
            if constexpr (doubly_linked) {
                long long size = static_cast<long long>(view_size);
                if (
                    (_step > 0 && _start <= size - closed) ||
                    (_step < 0 && size - _start <= closed)
                ) {
                    return std::make_pair(_start, closed);
                }
                return std::make_pair(closed, _start);
            }

            // if singly-linked, always start from head of list
            if (_step > 0) {
                return std::make_pair(_start, closed);
            }
            return std::make_pair(closed, _start);
        }
    };

private:
    friend View;
    View& view;

    SliceFactory(View& view) : view(view) {}
};


///////////////////////
////    PROXIES    ////
///////////////////////


/*
NOTE: SliceProxies are wrappers around a linked list, set, or dictionary that refer to
slices within the view.  They contain their own iterators, which can be used to
traverse the slice using standard `IteratorFactory` syntax.

One thing to note is that due to the singly-/doubly-linked nature of the list, the
direction of iteration may not always match the sign of the step size.  This is done to
minimize the total number of iterations required to traverse the slice without
backtracking.  Users will have to account for this (using the `inverted()` method)
when getting/setting items within the slice.
*/


/* A proxy that allows for efficient operations on slices within a list. */
template <typename ViewType>
class SliceProxy {
public:
    using View = ViewType;
    using Node = typename View::Node;
    using Indices = typename SliceFactory<View>::Indices;
    inline static constexpr bool doubly_linked = has_prev<Node>::value;

    // NOTE: Reverse iterators are only compiled for doubly-linked lists.

    template <
        Direction dir = Direction::forward,
        typename = std::enable_if_t<dir == Direction::forward || doubly_linked>
    >
    class Iterator;
    using IteratorPair = CoupledIterator<Bidirectional<Iterator>>;

    ////////////////////////////
    ////    PROXY ACCESS    ////
    ////////////////////////////

    /* Get the underlying view being referenced by the proxy. */
    inline View& view() const { return _view; }

    /* Pass through to Indices. */
    inline long long start() const { return indices.start(); }
    inline long long stop() const { return indices.stop(); }
    inline long long step() const { return indices.step(); }
    inline size_t abs_step() const { return indices.abs_step(); }
    inline size_t first() const { return indices.first(); }
    inline size_t last() const { return indices.last(); }
    inline size_t length() const { return indices.length(); }
    inline bool empty() const { return indices.empty(); }
    inline bool backward() const { return indices.backward(); }
    inline bool inverted() const { return indices.inverted(); }

    /////////////////////////
    ////    ITERATORS    ////
    /////////////////////////

    /* Return a coupled pair of iterators with a possible length override. */
    inline IteratorPair iter(std::optional<size_t> length = std::nullopt) const {
        using Forward = Iterator<Direction::forward>;

        // use length override if given
        if (length.has_value()) {
            size_t len = length.value();

            // backward traversal
            if constexpr (doubly_linked) {
                using Backward = Iterator<Direction::backward>;
                if (backward()) {
                    return IteratorPair(
                        Bidirectional(Backward(_view, origin(), indices, len)),
                        Bidirectional(Backward(_view, indices, len))
                    );
                }
            }

            // forward traversal
            return IteratorPair(
                Bidirectional(Forward(_view, origin(), indices, len)),
                Bidirectional(Forward(_view, indices, len))
            );
        }

        // default to length of slice
        return IteratorPair(begin(), end());
    }

    /* Return an iterator to the start of the slice. */
    inline Bidirectional<Iterator> begin() const {
        using Forward = Iterator<Direction::forward>;

        // account for empty sequence
        if (empty()) {
            return Bidirectional(Forward(_view, indices, length()));
        }

        // backward traversal
        if constexpr (doubly_linked) {
            using Backward = Iterator<Direction::backward>;
            if (backward()) {
                return Bidirectional(Backward(_view, origin(), indices, length()));
            }
        }

        // forward traversal
        return Bidirectional(Forward(_view, origin(), indices, length()));        
    }

    /* Return an iterator to the end of the slice. */
    inline Bidirectional<Iterator> end() const {
        using Forward = Iterator<Direction::forward>;

        // return same orientation as begin()
        if (empty()) {
            return Bidirectional(Forward(_view, indices, length()));
        }

        // backward traversal
        if constexpr (doubly_linked) {
            using Backward = Iterator<Direction::backward>;
            if (backward()) {
                return Bidirectional(Backward(_view, indices, length()));
            }
        }

        // forward traversal
        return Bidirectional(Forward(_view, indices, length()));
    }

    /////////////////////////////
    ////    INNER CLASSES    ////
    /////////////////////////////

    template <Direction dir>
    using BaseIterator = typename IndexFactory<View>::template Iterator<dir>;

    /* A specialized iterator built for slice traversal. */
    template <Direction dir, typename>
    class Iterator : public BaseIterator<dir> {
    public:
        using Base = BaseIterator<dir>;

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

        //////////////////////////////
        ////    HELPER METHODS    ////
        //////////////////////////////

        /* Remove the node at the current position. */
        inline Node* remove() { ++implicit_skip; return Base::remove(); }

        /* Indicates whether the direction of an Iterator matches the sign of the
        step size.

        If this is true, then the iterator will yield items in the same order as
        expected from the slice parameters.  Otherwise, it will yield items in the
        opposite order, and the user will have to account for this when getting/setting
        items within the list.  This is done to minimize the total number of iterations
        required to traverse the slice without backtracking. */
        inline bool inverted() const { return indices.inverted(); }

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
        const Indices& indices;
        size_t length_override;
        size_t implicit_skip;

        ////////////////////////////
        ////    CONSTRUCTORS    ////
        ////////////////////////////

        /* Get an iterator to the start of the slice. */
        Iterator(
            View& view,
            Node* origin,
            const Indices& indices,
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
        Iterator(View& view, const Indices& indices, size_t length_override) :
            Base(view, length_override), indices(indices),
            length_override(length_override), implicit_skip(0)
        {}

    };

private:
    friend View;
    friend SliceFactory<View>;
    View& _view;
    const Indices indices;
    mutable Node* _origin;  // node that immediately precedes the slice (can be NULL)
    mutable bool found;  // indicates whether we've cached the origin node

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    /* Construct a SliceProxy with at least one element. */
    SliceProxy(View& view, Indices&& indices) :
        _view(view), indices(indices), _origin(nullptr), found(false)
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
                Node* curr = _view.tail;
                for (size_t i = _view.size - 1; i > first(); i--) {
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
        Node* curr = _view.head;
        for (size_t i = 0; i < first(); i++) {
            prev = curr;
            curr = static_cast<Node*>(curr->next);
        }
        found = true;
        _origin = prev;
        return _origin;
    }

};


#endif  // BERTRAND_STRUCTS_CORE_SLICE_H include guard
