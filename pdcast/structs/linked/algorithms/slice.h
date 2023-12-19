#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_SLICE_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_SLICE_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <sstream>  // std::ostringstream
#include <stack>  // std::stack
#include <Python.h>  // CPython API
#include "../../util/except.h"  // TypeError()
#include "../../util/iter.h"  // iter()
#include "../../util/math.h"  // py_modulo()
#include "../../util/python.h"  // python::Dict, python::Slice
#include "../core/view.h"  // ViewTraits, Direction


namespace bertrand {
namespace linked {


    ///////////////////////////////////
    ////    SLICE NORMALIZATION    ////
    ///////////////////////////////////


    /* Data class representing normalized indices needed to construct a SliceProxy. */
    template <typename View>
    class SliceIndices {
        template <typename _View>
        friend SliceIndices<_View> normalize_slice(
            _View& view,
            std::optional<long long> start,
            std::optional<long long> stop,
            std::optional<long long> step
        );

        template <typename _View>
        friend SliceIndices<_View> normalize_slice(_View& view, PyObject* slice);

        /* Construct a SliceIndices object from normalized indices. */
        SliceIndices(
            long long start,
            long long stop,
            long long step,
            size_t length,
            size_t vsize
        ) : start(start), stop(stop), step(step), abs_step(std::llabs(step)),
            first(0), last(0), length(length), inverted(false), backward(false)
        {
            // convert from half-open to closed interval
            long long mod = bertrand::util::py_modulo((stop - start), step);
            long long nstop = (mod == 0) ? (stop - step) : (stop - mod);

            // flip start/stop based on singly-/doubly-linked status
            if constexpr (NodeTraits<typename View::Node>::has_prev) {
                long long lsize = static_cast<long long>(vsize);
                inverted = (
                    (step > 0 && lsize - nstop <= start) ||
                    (step < 0 && nstop <= lsize - start)
                );
                first = inverted ? nstop : start;
                last = inverted ? start : nstop;
                backward = first > last || (first == last && first > (vsize + 1) / 2);
            } else {
                inverted = step < 0;
                first = inverted ? nstop : start;
                last = inverted ? start : nstop;
            }
        }

    public:
        long long start, stop, step;
        size_t abs_step;
        size_t first, last;
        size_t length;
        bool inverted;
        bool backward;
    };


    /* Normalize slice indices, applying Python-style wraparound and bounds
    checking. */
    template <typename View>
    SliceIndices<View> normalize_slice(
        View& view,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt,
        std::optional<long long> step = std::nullopt
    ) {
        long long lsize = view.size();
        long long default_start = step.value_or(0) < 0 ? lsize - 1 : 0;
        long long default_stop = step.value_or(0) < 0 ? -1 : lsize;

        long long nstep = step.value_or(1);
        if (nstep == 0) throw ValueError("slice step cannot be zero");

        long long nstart = start.value_or(default_start);
        if (nstart < 0) {
            nstart += lsize;
            if (nstart < 0) {
                nstart = nstep < 0 ? -1 : 0;
            }
        } else if (nstart >= lsize) {
            nstart = nstep < 0 ? lsize - 1 : lsize;
        }

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
    inline SliceIndices<View> normalize_slice(View& view, PyObject* slice) {
        using Indices = std::tuple<long long, long long, long long, size_t>;

        python::Slice<python::Ref::BORROW> py_slice(slice);
        Indices indices(py_slice.normalize(view.size()));

        return SliceIndices<View>(
            std::get<0>(indices),
            std::get<1>(indices),
            std::get<2>(indices),
            std::get<3>(indices),
            view.size()
        );
    }


    /////////////////////
    ////    PROXY    ////
    /////////////////////


    /* A proxy for a slice within a list, as returned by the slice() factory method. */
    template <typename View, typename Result, Yield yield, bool as_pytuple = false>
    class SliceProxy {
        static_assert(
            !(Result::FLAGS & Config::FIXED_SIZE),
            "slice().get() must return a dynamically-sized container for consistency "
            "with other algorithms that produce a new data structure"
        );

        using Node = std::conditional_t<
            std::is_const_v<View>,
            const typename View::Node,
            typename View::Node
        >;

        template <Direction dir>
        using ViewIter = linked::Iterator<View, dir, yield, as_pytuple>;

        View& view;
        const SliceIndices<View> indices;
        mutable bool cached;
        mutable Node* _origin;

        template <
            typename _Result,
            Yield _yield,
            bool _as_pyobject,
            typename _View,
            typename... Args
        >
        friend auto slice(_View& view, Args&&... args)
            -> std::enable_if_t<
                ViewTraits<_View>::linked,
                std::conditional_t<
                    std::is_const_v<_View>,
                    const SliceProxy<_View, _Result, _yield, _as_pyobject>,
                    SliceProxy<_View, _Result, _yield, _as_pyobject>
                >
            >;

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

        /* Container-independent implementation for slice().set(). */
        template <typename Container>
        void _set_impl(const Container& items) {
            using MemGuard = typename View::MemGuard;

            // unpack items into indexable sequence with known length
            auto seq = sequence(items);  // possibly a no-op if already a sequence
            if (indices.length != seq.size()) {
                // NOTE: Python allows the slice and sequence lengths to differ if and
                // only if the step size is 1.  This can possibly change the overall
                // length of the list, and makes everything much more complicated.
                if (indices.step != 1) {
                    std::ostringstream msg;
                    msg << "attempt to assign sequence of size " << seq.size();
                    msg << " to extended slice of size " << indices.length;
                    throw ValueError(msg.str());
                }
            } else if (indices.length == 0) {
                return;
            }

            // allocate recovery array and freeze list allocator during iteration
            RecoveryArray recovery(indices.length);
            size_t fsize = view.size() - indices.length + seq.size();
            MemGuard guard = view.reserve(fsize < view.size() ? view.size() : fsize);

            // if doubly-linked, then we can potentially use a reverse iterator
            if constexpr (NodeTraits<Node>::has_prev) {
                if (indices.backward) {
                    auto iter = [&]() -> ViewIter<Direction::BACKWARD> {
                        Node* next = origin();
                        Node* curr = (next == nullptr) ? view.tail() : next->prev();
                        Node* prev = (curr == nullptr) ? nullptr : curr->prev();
                        return ViewIter<Direction::BACKWARD>(view, prev, curr, next);
                    };

                    // remove current occupants
                    if (indices.length > 0) {
                        size_t idx = 0;
                        auto loop1 = iter();
                        while (true) {
                            Node* node = loop1.drop();
                            new (&recovery[idx]) Node(std::move(*node));
                            view.recycle(node);
                            if (++idx == indices.length) {
                                break;
                            }
                            for (size_t i = 1; i < indices.abs_step; ++i, ++loop1);
                        }
                    }

                    // insert new nodes from sequence
                    if (seq.size() > 0) {
                        size_t idx = 0;
                        try {
                            auto loop2 = iter();
                            while (true) {
                                size_t i = indices.inverted ? seq.size() - idx - 1 : idx;
                                loop2.insert(view.node(seq[i]));
                                if (++idx == seq.size()) {
                                    break;
                                }
                                for (size_t i = 0; i < indices.abs_step; ++i, ++loop2);
                            }
                        } catch (...) {
                            // remove nodes that have already been added
                            if (idx > 0) {
                                size_t i = 0;
                                auto loop3 = iter();
                                while (true) {
                                    view.recycle(loop3.drop());
                                    if (++i == idx) {
                                        break;
                                    }
                                    for (size_t i = 1; i < indices.abs_step; ++i, ++loop3);
                                }
                            }

                            // reinsert originals from recovery array
                            if (indices.length > 0) {
                                size_t i = 0;
                                auto loop4 = iter();
                                while (true) {
                                    loop4.insert(view.node(std::move(recovery[i])));
                                    if (++i == indices.length) {
                                        break;
                                    }
                                    for (size_t i = 0; i < indices.abs_step; ++i, ++loop4);
                                }
                            }
                            throw;
                        }
                    }

                    // deallocate recovery array
                    for (size_t idx = 0; idx < indices.length; ++idx) {
                        recovery[idx].~Node();
                    }
                    return;
                }
            }

            // otherwise we do the same thing, but with a forward iterator
            auto iter = [&]() -> ViewIter<Direction::FORWARD> {
                Node* prev = origin();
                Node* curr = (prev == nullptr) ? view.head() : prev->next();
                Node* next = (curr == nullptr) ? nullptr : curr->next();
                return ViewIter<Direction::FORWARD>(view, prev, curr, next);
            };

            // remove current occupants
            if (indices.length > 0) {
                size_t idx = 0;
                auto loop1 = iter();
                while (true) {
                    Node* node = loop1.drop();
                    new (&recovery[idx]) Node(std::move(*node));
                    view.recycle(node);
                    if (++idx == indices.length) {
                        break;
                    }
                    for (size_t i = 1; i < indices.abs_step; ++i, ++loop1);
                }
            }

            // insert new nodes from sequence
            if (seq.size() > 0) {
                size_t idx = 0;
                try {
                    auto loop2 = iter();
                    while (true) {
                        size_t i = indices.inverted ? seq.size() - idx - 1 : idx;
                        loop2.insert(view.node(seq[i]));
                        if (++idx == seq.size()) {
                            break;
                        }
                        for (size_t i = 0; i < indices.abs_step; ++i, ++loop2);
                    }

                } catch (...) {
                    // remove nodes that have already been added
                    if (idx > 0) {
                        size_t i = 0;
                        auto loop3 = iter();
                        while (true) {
                            view.recycle(loop3.drop());
                            if (++i == idx) {
                                break;
                            }
                            for (size_t i = 1; i < indices.abs_step; ++i, ++loop3);
                        }
                    }

                    // reinsert originals from recovery array
                    if (indices.length > 0) {
                        size_t i = 0;
                        auto loop4 = iter();
                        while (true) {
                            loop4.insert(view.node(std::move(recovery[i])));
                            if (++i == indices.length) {
                                break;
                            }
                            for (size_t i = 0; i < indices.abs_step; ++i, ++loop4);
                        }
                    }
                    throw;
                }
            }

            // deallocate recovery array
            for (size_t idx = 0; idx < indices.length; ++idx) {
                recovery[idx].~Node();
            }
        }

    public:
        SliceProxy(const SliceProxy&) = delete;
        SliceProxy(SliceProxy&&) = delete;
        SliceProxy& operator=(const SliceProxy&) = delete;
        SliceProxy& operator=(SliceProxy&&) = delete;

        /////////////////////////
        ////    ITERATORS    ////
        /////////////////////////

        /* A specialized iterator that directly traverses over a slice without any
        copies.  This automatically corrects for inverted traversal and always yields
        items in the same order as the step size (reversed if called from rbegin). */
        template <Direction dir>
        class Iterator {
            using Value = typename View::Value;

            /* Infer dereference type based on `yield` parameter. */
            template <Yield Y = Yield::KEY, typename Dummy = void>
            struct DerefType {
                using type = typename Node::Value;
            };
            template <typename Dummy>
            struct DerefType<Yield::VALUE, Dummy> {
                using type = typename Node::MappedValue;
            };
            template <typename Dummy>
            struct DerefType<Yield::ITEM, Dummy> {
                using type = std::conditional_t<
                    as_pytuple,
                    python::Tuple<python::Ref::STEAL>,
                    std::pair<typename Node::Value, typename Node::MappedValue>
                >;
            };

            using Deref = std::conditional_t<
                std::is_const_v<View>,
                const typename DerefType<yield>::type,
                typename DerefType<yield>::type
            >;

            /* NOTE: this iterator is tricky.  It is essentially a wrapper around a
             * standard view iterator, but it must also handle inverted traversal and
             * negative step sizes.  This means that the direction of the view iterator
             * might not match the direction of the slice iterator, depending on the
             * singly-/doubly-linked status of the list and the input to the slice()
             * factory method.
             *
             * To handle this, we use a type-erased union of view iterators and a stack
             * of nodes that can be used to cancel out inverted traversal.  The stack
             * is only populated if necessary, and also takes into account whether the
             * iterator is generated from the begin()/end() or rbegin()/rend() methods.
             *
             * These iterators are only meant for direct iteration (no copying) over
             * the slice.  The get(), set() and del() methods use the view iterators
             * instead, and have more efficient methods of handling inverted traversal
             * that don't require any backtracking or auxiliary data structures.
             */

            union {
                ViewIter<Direction::FORWARD> fwd;
                ViewIter<Direction::BACKWARD> bwd;
            };

            std::stack<Node*> stack;
            const View& view;
            const SliceIndices<View> indices;
            size_t idx;

            friend SliceProxy;

            /* Get an iterator to the start of a non-empty slice. */
            Iterator(View& view, const SliceIndices<View>& indices, Node* origin) :
                view(view), indices(indices), idx(0)
            {
                if (!indices.backward) {
                    Node* prev = origin;
                    Node* curr = (prev == nullptr) ? view.head() : prev->next();
                    Node* next = (curr == nullptr) ? nullptr : curr->next();
                    new (&fwd) ViewIter<Direction::FORWARD>(view, prev, curr, next);

                    // use stack to cancel out inverted traversal
                    if (indices.inverted ^ (dir == Direction::BACKWARD)) {
                        while (true) {
                            stack.push(fwd.curr());
                            if (++idx >= indices.length) {
                                break;
                            }
                            for (size_t i = 0; i < indices.abs_step; ++i, ++fwd);
                        }
                        idx = 0;
                    }

                } else {
                    if constexpr (NodeTraits<Node>::has_prev) {
                        Node* next = origin;
                        Node* curr = (next == nullptr) ? view.tail() : next->prev();
                        Node* prev = (curr == nullptr) ? nullptr : curr->prev();
                        new (&bwd) ViewIter<Direction::BACKWARD>(view, prev, curr, next);

                        // use stack to cancel out inverted traversal
                        if (indices.inverted ^ (dir == Direction::BACKWARD)) {
                            while (true) {
                                stack.push(bwd.curr());
                                if (++idx >= indices.length) {
                                    break;
                                }
                                for (size_t i = 0; i < indices.abs_step; ++i, ++bwd);
                            }
                            idx = 0;
                        }

                    // unreachable: indices.backward is always false if singly-linked
                    } else {
                        throw ValueError(
                            "backwards traversal is not supported for "
                            "singly-linked lists"
                        );
                    }
                }
            }

            /* Get an empty iterator to terminate the slice. */
            Iterator(View& view, const SliceIndices<View>& indices) :
                view(view), indices(indices), idx(indices.length)
            {}

        public:
            using iterator_tag          = std::forward_iterator_tag;
            using difference_type       = std::ptrdiff_t;
            using value_type            = std::remove_reference_t<Deref>;
            using pointer               = value_type*;
            using reference             = value_type&;

            /* Copy constructor. */
            Iterator(const Iterator& other) noexcept :
                stack(other.stack), view(other.view), indices(other.indices),
                idx(other.idx)
            {
                if (indices.backward) {
                    new (&bwd) ViewIter<Direction::BACKWARD>(other.bwd);
                } else {
                    new (&fwd) ViewIter<Direction::FORWARD>(other.fwd);
                }
            }

            /* Move constructor. */
            Iterator(Iterator&& other) noexcept :
                stack(std::move(other.stack)), view(other.view),
                indices(other.indices), idx(other.idx)
            {
                if (indices.backward) {
                    new (&bwd) ViewIter<Direction::BACKWARD>(std::move(other.bwd));
                } else {
                    new (&fwd) ViewIter<Direction::FORWARD>(std::move(other.fwd));
                }
            }

            /* Clean up union iterator on destruction. */
            ~Iterator() {
                if (indices.backward) {
                    bwd.~ViewIter<Direction::BACKWARD>();
                } else {
                    fwd.~ViewIter<Direction::FORWARD>();
                }
            }

            /* Dereference the iterator to get the value at the current index. */
            inline Deref operator*() const {
                if (!stack.empty()) {
                    Node* node = stack.top();
                    if constexpr (yield == Yield::KEY) {
                        return node->value();
                    } else if constexpr (yield == Yield::VALUE) {
                        return node->mapped();
                    } else {
                        if constexpr (as_pytuple) {
                            using PyTuple = python::Tuple<python::Ref::STEAL>;
                            return PyTuple::pack(node->value(), node->mapped());
                        } else {
                            return std::make_pair(node->value(), node->mapped());
                        }
                    }
                } else {
                    return indices.backward ? *bwd : *fwd;
                }
            }

            /* Advance the iterator to the next element in the slice. */
            inline Iterator& operator++() noexcept {
                ++idx;
                if (!stack.empty()) {
                    stack.pop();
                } else if (idx < indices.length) {  // don't advance on final iteration
                    if (indices.backward) {
                        for (size_t i = 0; i < indices.abs_step; ++i, ++bwd);
                    } else {
                        for (size_t i = 0; i < indices.abs_step; ++i, ++fwd);
                    }
                }
                return *this;
            }

            /* Compare iterators to terminate the slice. */
            inline bool operator!=(const Iterator& other) const noexcept {
                return idx != other.idx;
            }

        };

        inline Iterator<Direction::FORWARD> begin() const {
            if (indices.length == 0) {
                return end();
            }
            return Iterator<Direction::FORWARD>(view, indices, origin());
        }

        inline Iterator<Direction::FORWARD> cbegin() const {
            return begin();
        }

        inline Iterator<Direction::FORWARD> end() const {
            return Iterator<Direction::FORWARD>(view, indices);
        }

        inline Iterator<Direction::FORWARD> cend() const {
            return end();
        }

        inline Iterator<Direction::BACKWARD> rbegin() const {
            if (indices.length == 0) {
                return rend();
            }
            return Iterator<Direction::BACKWARD>(view, indices, origin());
        }

        inline Iterator<Direction::BACKWARD> crbegin() const {
            return rbegin();
        }

        inline Iterator<Direction::BACKWARD> rend() const {
            return Iterator<Direction::BACKWARD>(view, indices);
        }

        inline Iterator<Direction::BACKWARD> crend() const {
            return rend();
        }

        //////////////////////
        ////    PUBLIC    ////
        //////////////////////

        /* Extract a slice from a linked list. */
        Result get() const {
            Result result(indices.length, view.specialization());
            if (indices.length == 0) {
                return result;
            }

            // helper for generating an appropriate node from a view iterator
            auto parse = [&result](const auto& it) {
                if constexpr (std::is_same_v<
                    std::remove_const_t<View>, typename Result::View
                >) {
                    return result.view.node(*(it.curr()));
                } else {
                    return result.view.node(*it);
                }
            };

            // if doubly-linked, then we can potentially use a reverse iterator
            if constexpr (NodeTraits<Node>::has_prev) {
                if (indices.backward) {
                    size_t idx = 0;
                    Node* next = origin();
                    Node* curr = (next == nullptr) ? view.tail() : next->prev();
                    Node* prev = (curr == nullptr) ? nullptr : curr->prev();
                    ViewIter<Direction::BACKWARD> it(view, prev, curr, next);
                    if (indices.inverted) {
                        while (true) {
                            result.view.link(nullptr, parse(it), result.view.head());
                            if (++idx == indices.length) {
                                break;
                            }
                            for (size_t i = 0; i < indices.abs_step; ++i, ++it);
                        }
                    } else {
                        while (true) {
                            result.view.link(result.view.tail(), parse(it), nullptr);
                            if (++idx == indices.length) {
                                break;
                            }
                            for (size_t i = 0; i < indices.abs_step; ++i, ++it);
                        }
                    }
                    return result;
                }
            }

            // otherwise, we use a forward iterator
            size_t idx = 0;
            Node* prev = origin();
            Node* curr = (prev == nullptr) ? view.head() : prev->next();
            Node* next = (curr == nullptr) ? nullptr : curr->next();
            ViewIter<Direction::FORWARD> it(view, prev, curr, next);
            if (indices.inverted) {
                while (true) {
                    result.view.link(nullptr, parse(it), result.view.head());
                    if (++idx == indices.length) {
                        break;
                    }
                    for (size_t i = 0; i < indices.abs_step; ++i, ++it);
                }
            } else {
                while (true) {
                    result.view.link(result.view.tail(), parse(it), nullptr);
                    if (++idx == indices.length) {
                        break;
                    }
                    for (size_t i = 0; i < indices.abs_step; ++i, ++it);
                }
            }
            return result;
        }

        /* Replace a slice within a linked list. */
        template <typename Container>
        inline void set(const Container& items) {
            if constexpr (ViewTraits<View>::dictlike && is_pyobject<Container>) {
                if (PyDict_Check(items)) {
                    python::Dict<python::Ref::BORROW> dict(items);
                    _set_impl(dict);
                    return;
                }
            }
            _set_impl(items);
        }

        /* Delete a slice within a linked list. */
        void del() {
            if (indices.length > 0) {
                typename View::MemGuard guard = view.reserve();

                // if doubly-linked, then we can potentially use a reverse iterator
                if constexpr (NodeTraits<Node>::has_prev) {
                    if (indices.backward) {
                        size_t idx = 0;
                        Node* next = origin();
                        Node* curr = (next == nullptr) ? view.tail() : next->prev();
                        Node* prev = (curr == nullptr) ? nullptr : curr->prev();
                        ViewIter<Direction::BACKWARD> it(view, prev, curr, next);
                        while (true) {
                            view.recycle(it.drop());
                            if (++idx == indices.length) {
                                break;
                            }
                            for (size_t i = 1; i < indices.abs_step; ++i, ++it);
                        }
                        return;
                    }
                }

                // otherwise, we use a forward iterator
                size_t idx = 0;
                Node* prev = origin();
                Node* curr = (prev == nullptr) ? view.head() : prev->next();
                Node* next = (curr == nullptr) ? nullptr : curr->next();
                ViewIter<Direction::FORWARD> it(view, prev, curr, next);
                while (true) {
                    view.recycle(it.drop());
                    if (++idx == indices.length) {
                        break;
                    }
                    for (size_t i = 1; i < indices.abs_step; ++i, ++it);
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
    template <
        typename Result,
        Yield yield = Yield::KEY,
        bool as_pytuple = false,
        typename View,
        typename... Args
    >
    inline auto slice(View& view, Args&&... args)
        -> std::enable_if_t<
            ViewTraits<View>::linked,
            std::conditional_t<
                std::is_const_v<View>,
                const SliceProxy<View, Result, yield, as_pytuple>,
                SliceProxy<View, Result, yield, as_pytuple>
            >
        >
    {
        return SliceProxy<View, Result, yield, as_pytuple>(
            view, normalize_slice(view, std::forward<Args>(args)...))
        ;
    }


}  // namespace linked
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_SLICE_H
