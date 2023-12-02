#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_SLICE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_SLICE_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <sstream>  // std::ostringstream
#include <Python.h>  // CPython API
#include "../core/iter.h"  // Bidirectional<>
#include "../core/view.h"  // ViewTraits
#include "../../util/container.h"  // PySequence
#include "../../util/except.h"  // TypeError()
#include "../../util/iter.h"  // iter()
#include "../../util/math.h"  // py_modulo()


namespace bertrand {
namespace structs {
namespace linked {


    ///////////////////////////////////
    ////    SLICE NORMALIZATION    ////
    ///////////////////////////////////


    /* Data class representing normalized indices needed to construct a SliceProxy. */
    template <typename View>
    class SliceIndices {
        template <typename _View>
        friend SliceIndices<_View> normalize_slice(
            const _View& view,
            std::optional<long long> start,
            std::optional<long long> stop,
            std::optional<long long> step
        );

        template <typename _View>
        friend SliceIndices<_View> normalize_slice(const _View& view, PyObject* slice);

        /* Construct a SliceIndices object from normalized indices. */
        SliceIndices(
            long long start,
            long long stop,
            long long step,
            size_t length,
            size_t view_size
        ) : start(start), stop(stop), step(step), abs_step(std::llabs(step)),
            first(0), last(0), length(length), inverted(false), backward(false)
        {
            using Node = typename View::Node;

            // make closed interval
            long long mod = bertrand::util::py_modulo((stop - start), step);
            long long closed = (mod == 0) ? (stop - step) : (stop - mod);

            // flip start/stop based on singly-/doubly-linked status
            if constexpr (NodeTraits<Node>::has_prev) {
                long long lsize = static_cast<long long>(view_size);
                bool cond = (
                    (step > 0 && start <= lsize - closed) ||
                    (step < 0 && lsize - start <= closed)
                );
                first = cond ? start : closed;
                last = cond ? closed : start;
            } else {
                first = step > 0 ? start : closed;
                last = step > 0 ? closed : start;
            }

            backward = (first > ((view_size - (view_size > 0)) / 2));
            inverted = backward ^ (step < 0);
        }

    public:
        long long start, stop, step;  // original indices supplied to constructor
        size_t abs_step;
        size_t first, last;  // first and last indices included in slice
        size_t length;  // total number of items
        bool inverted;  // if true, first and last indices contradict step size
        bool backward;  // if true, traverse from tail

    };


    /* Normalize slice indices, applying Python-style wraparound and bounds
    checking. */
    template <typename View>
    SliceIndices<View> normalize_slice(
        const View& view,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt,
        std::optional<long long> step = std::nullopt
    ) {
        // apply defaults
        long long lsize = static_cast<long long>(view.size());
        long long default_start = step.value_or(0) < 0 ? lsize - 1 : 0;
        long long default_stop = step.value_or(0) < 0 ? -1 : lsize;

        // normalize step
        long long nstep = step.value_or(1);
        if (nstep == 0) throw ValueError("slice step cannot be zero");

        // normalize start
        long long nstart = start.value_or(default_start);
        if (nstart < 0) {
            nstart += lsize;
            if (nstart < 0) {
                nstart = nstep < 0 ? -1 : 0;
            }
        } else if (nstart >= lsize) {
            nstart = nstep < 0 ? lsize - 1 : lsize;
        }

        // normalize stop
        long long nstop = stop.value_or(default_stop);
        if (nstop < 0) {
            nstop += lsize;
            if (nstop < 0) {
                nstop = nstep < 0 ? -1 : 0;
            }
        } else if (nstop > lsize) {
            nstop = nstep < 0 ? lsize - 1 : lsize;
        }

        long long length = (nstop - nstart + nstep - (nstep > 0 ? 1 : -1)) / nstep;

        return SliceIndices<View>(nstart, nstop, nstep, length < 0 ? 0 : length, lsize);
    }


    /* Normalize a Python slice object, applying Python-style wraparound and bounds
    checking. */
    template <typename View>
    SliceIndices<View> normalize_slice(const View& view, PyObject* slice) {
        if (!PySlice_Check(slice)) {
            throw TypeError("index must be a Python slice");
        }

        size_t size = view.size();
        Py_ssize_t start, stop, step, length;
        if (PySlice_GetIndicesEx(slice, size, &start, &stop, &step, &length)) {
            throw catch_python();
        }

        return SliceIndices<View>(start, stop, step, length, size);
    }


    /////////////////////
    ////    PROXY    ////
    /////////////////////


    // TODO: I appear to have broken slice assignment somehow.  Not sure exactly how,
    // though.  The second assignment always breaks things.


    /* A proxy for a slice within a list, as returned by the slice() factory method. */
    template <typename View, typename Result>
    class SliceProxy {
        using Node = typename View::Node;
        static constexpr Direction forward = Direction::forward;
        static constexpr Direction backward = Direction::backward;

        template <typename _View, typename _Result, typename... Args>
        friend auto slice(_View& view, Args&&... args)
            -> std::enable_if_t<ViewTraits<_View>::linked, SliceProxy<_View, _Result>>;

        View& view;
        const SliceIndices<View> indices;
        mutable bool cached;
        mutable Node* _origin;  // node immediately preceding slice (can be null)

        /* Construct a SliceProxy using the normalized indices. */
        SliceProxy(View& view, SliceIndices<View>&& indices) :
            view(view), indices(indices), cached(false), _origin(nullptr)
        {}

        /* Find the origin node for the slice. */
        Node* origin() const {
            if (cached) {
                return _origin;
            }

            if constexpr (NodeTraits<Node>::has_prev) {
                if (indices.backward) {
                    auto it = view.rbegin();
                    for (size_t i = 1; i < view.size() - indices.first; ++i, ++it);
                    cached = true;
                    _origin = it.next();
                    return _origin;
                }
            }

            auto it = view.begin();
            for (size_t i = 0; i < indices.first; ++i, ++it);
            cached = true;
            _origin = it.prev();
            return _origin;
        }

        /* Simple C array used in set() to hold a temporary buffer of replaced nodes. */
        struct RecoveryArray {
            Node* array;
    
            RecoveryArray(size_t length) {
                array = static_cast<Node*>(malloc(sizeof(Node) * length));
                if (array == nullptr) throw MemoryError();
            }

            Node& operator[](size_t index) {
                return array[index];
            }

            ~RecoveryArray() {
                free(array);  // no need to call destructors
            }

        };

    public:
        /* Disallow SliceProxies from being stored as lvalues. */
        SliceProxy(const SliceProxy&) = delete;
        SliceProxy(SliceProxy&&) = delete;
        SliceProxy& operator=(const SliceProxy&) = delete;
        SliceProxy& operator=(SliceProxy&&) = delete;

        /////////////////////////
        ////    ITERATORS    ////
        /////////////////////////

        /* A specialized iterator built for slice traversal. */
        template <Direction dir>
        class Iterator : public View::template Iterator<dir> {
            using Base = typename View::template Iterator<dir>;
            friend SliceProxy;

            const SliceIndices<View> indices;
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

            // TODO: might be a case for a while true loop with manual termination
            // to avoid if check.

            /* Advance the iterator. */
            inline Iterator& operator++() noexcept {
                if (++idx == indices.length) {
                    return *this;  // don't move past end of slice
                }

                for (size_t i = skip; i < indices.abs_step; ++i) {
                    if constexpr (dir == Direction::backward) {
                        this->_next = this->_curr;
                        this->_curr = this->_prev;
                        this->_prev = this->_curr->prev();
                    } else {
                        this->_prev = this->_curr;
                        this->_curr = this->_next;
                        this->_next = this->_curr->next();
                    }
                }
                return *this;
            }

            /* Terminate the slice. */
            template <Direction T>
            inline bool operator!=(const Iterator<T>& other) const noexcept {
                return idx != other.idx;
            }

            /* Get the current index of the iterator within the list. */
            inline size_t index() const noexcept {
                if (dir == Direction::backward) {
                    return indices.first - idx * indices.abs_step;
                } else {
                    return indices.first + idx * indices.abs_step;
                }
            }

            /* Unlink and return the node at the current position. */
            inline Node* drop() {
                ++skip;
                return Base::drop();
            }

        };

        /* Return an iterator to the start of the slice. */
        inline auto begin() const {
            if constexpr (NodeTraits<Node>::has_prev) {
                if (indices.backward) {
                    return Bidirectional(Iterator<backward>(view, indices, origin()));
                }
            }
            return Bidirectional(Iterator<forward>(view, indices, origin()));        
        }

        /* Return an iterator to the end of the slice. */
        inline auto end() const {
            if constexpr (NodeTraits<Node>::has_prev) {
                if (indices.backward) {
                    return Bidirectional(Iterator<backward>(view, indices));
                }
            }
            return Bidirectional(Iterator<forward>(view, indices));
        }

        //////////////////////
        ////    PUBLIC    ////
        //////////////////////

        /* Extract a slice from a linked list. */
        Result get() const {
            // preallocate to exact size
            View result(indices.length, view.specialization());
            if (indices.length == 0) {
                if constexpr (std::is_same_v<View, Result>) {
                    return result;
                } else {
                    return Result(std::move(result));
                }
            }

            // copy all nodes in slice
            for (auto it = this->begin(), end = this->end(); it != end; ++it) {
                Node* copy = result.node(*(it.curr()));
                if (indices.inverted) {
                    result.link(nullptr, copy, result.head());  // cancels out
                } else {
                    result.link(result.tail(), copy, nullptr);
                }
            }

            // possibly wrap in higher-level container
            if constexpr (std::is_same_v<View, Result>) {
                return result;
            } else {
                return Result(std::move(result));
            }
        }

        /* Replace a slice within a linked list. */
        template <typename Container>
        void set(const Container& items) {
            using MemGuard = typename View::MemGuard;

            // unpack items into indexable sequence with known length
            auto seq = sequence(items);  // possibly a no-op if already a sequence
            if (indices.length != seq.size()) {
                // NOTE: Python allows the slice and sequence lengths to differ if and
                // only if the step size is 1.  This can possibly change the overall
                // length of the list.
                if (indices.step != 1) {
                    std::ostringstream msg;
                    msg << "attempt to assign sequence of size " << seq.size();
                    msg << " to extended slice of size " << indices.length;
                    throw ValueError(msg.str());
                }
            } else if (indices.length == 0) {
                return;
            }

            RecoveryArray recovery(indices.length);
            MemGuard guard = view.reserve(view.size() - indices.length + seq.size());

            // remove current occupants
            size_t idx = 0;
            for (auto it = this->begin(); idx < indices.length; ++idx, ++it) {
                Node* node = it.drop();
                new (&recovery[idx]) Node(std::move(*node));
                view.recycle(node);
            }

            // insert new nodes from sequence
            try {
                idx = 0;
                for (auto it = this->begin(); idx < seq.size(); ++idx, ++it) {
                    it.insert(
                        view.node(seq[indices.inverted ? seq.size() - idx - 1 : idx])
                    );
                }

            } catch (...) {
                // remove nodes that have already been added
                size_t i = 0;
                for (auto it = this->begin(); i < idx; ++i, ++it) {
                    view.recycle(it.drop());
                }

                // reinsert originals from recovery array
                i = 0;
                for (auto it = this->begin(); i < indices.length; ++i, ++it) {
                    it.insert(view.node(std::move(recovery[i])));
                }
                throw;
            }

            // deallocate recovery array
            for (size_t i = 0; i < indices.length; i++) {
                recovery[i].~Node();
            }
        }

        /* Delete a slice within a linked list. */
        void del() {
            // freeze allocator during iteration
            if (indices.length != 0) {
                typename View::MemGuard guard = view.reserve();
                for (auto it = this->begin(), end = this->end(); it != end; ++it) {
                    view.recycle(it.drop());
                }
            }
        }

        /* Implicitly convert the proxy into a result where applicable.  This is
        syntactic sugar for get(), such that `LinkedList<T> list = list.slice(i, j, k)`
        is equivalent to `LinkedList<T> list = list.slice(i, j, k).get()`. */
        inline operator Result() const {
            return get();
        }

        /* Assign the slice in-place.  This is syntactic sugar for set(), such that
        `list.slice(i, j, k) = items` is equivalent to
        `list.slice(i, j, k).set(items)`. */
        template <typename Container>
        inline SliceProxy& operator=(const Container& items) {
            set(items);
            return *this;
        }

    };


    /* Get a proxy for a slice within the list. */
    template <typename View, typename Result = View, typename... Args>
    auto slice(View& view, Args&&... args)
        -> std::enable_if_t<ViewTraits<View>::linked, SliceProxy<View, Result>>
    {
        return SliceProxy<View, Result>(
            view, normalize_slice(view, std::forward<Args>(args)...))
        ;
    }


    /* Get a const proxy for a slice within a const list. */
    template <typename View, typename Result = View, typename... Args>
    auto slice(const View& view, Args&&... args)
        -> std::enable_if_t<ViewTraits<View>::linked, const SliceProxy<View, Result>>
    {
        return SliceProxy<View, Result>(
            view, normalize_slice(view, std::forward<Args>(args)...)
        );
    }


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_SLICE_H
