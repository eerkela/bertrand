// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_BASE_H
#define BERTRAND_STRUCTS_BASE_H

#include <cstddef>      // size_t
#include <optional>     // std::optional
#include <stdexcept>    // std::runtime_error
#include <string_view>  // std::string_view
#include "core/util.h"  // string concatenation, CoupledIterator, PyIterator


/* Base class that forwards the public members of the underlying view. */
template <typename View, const std::string_view& name>
class LinkedBase {
public:
    /* Every LinkedList contains a view that manages low-level node
    allocation/deallocation and links between nodes. */
    View view;

    /* Check if the list contains any elements. */
    inline bool empty() const {
        return view.size() == 0;
    }

    /* Get the current size of the list. */
    inline size_t size() const {
        return view.size();
    }

    /* Get the current capacity of the allocator array. */
    inline size_t capacity() const {
        return view.capacity();
    }

    /* Get the maximum size of the list. */
    inline std::optional<size_t> max_size() const {
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
    inline PyObject* specialization() const {
        return view.specialization();  // TODO: reference counting?
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
        /* A wrapper around a view iterator that yields values rather than nodes. */
        template <Direction dir>
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
            return CoupledIterator<Iterator<Direction::forward>>(parent.iter());
        }

        /* Get a coupled reverse iterator over the list. */
        inline auto reverse() const {
            return CoupledIterator<Iterator<Direction::backward>>(parent.iter.reverse());
        }

        /* Get a forward iterator to the head of the list. */
        inline auto begin() const {
            return Iterator<Direction::forward>(parent.view.begin());
        }

        /* Get an empty forward iterator to terminate the sequence. */
        inline auto end() const {
            return Iterator<Direction::forward>(parent.view.end());
        }

        /* Get a backward iterator to the tail of the list. */
        inline auto rbegin() const {
            return Iterator<Direction::backward>(parent.view.rbegin());
        }

        /* Get an empty backward iterator to terminate the sequence. */
        inline auto rend() const {
            return Iterator<Direction::backward>(parent.view.rend());
        }

        /* Get a forward Python iterator over the list. */
        inline PyObject* python() const {
            static constexpr std::string_view suffix = ".iter";
            using Iter = PyIterator<
                Iterator<Direction::forward>, String::concat_v<name, suffix>
            >;

            // create Python iterator over list
            Iter* iter = Iter::create(begin(), end());
            if (iter == nullptr) {
                throw std::runtime_error("could not create iterator instance");
            }

            // return as PyObject*
            return reinterpret_cast<PyObject*>(iter);
        }

        /* Get a reverse Python iterator over the list. */
        inline PyObject* rpython() const {
            static constexpr std::string_view suffix = ".reverse_iter";
            using Iter = PyIterator<
                Iterator<Direction::backward>, String::concat_v<name, suffix>
            >;

            // create Python iterator over list
            Iter* iter = Iter::create(rbegin(), rend());
            if (iter == nullptr) {
                throw std::runtime_error("could not create iterator instance");
            }

            // return as PyObject*
            return reinterpret_cast<PyObject*>(iter);
        }

    private:
        friend LinkedBase;
        LinkedBase<View, name>& parent;

        IteratorFactory(LinkedBase<View, name>& parent) : parent(parent) {}
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


#endif  // BERTRAND_STRUCTS_BASE_H
