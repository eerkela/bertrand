
// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_SET_H
#define BERTRAND_STRUCTS_SET_H

#include <cstddef>  // for size_t
#include <memory>
#include <utility>  // std::pair
#include <variant>  // std::variant
#include <Python.h>  // CPython API

// Algorithms
#include "algorithms/add.h"
#include "algorithms/clear.h"
#include "algorithms/compare.h"
#include "algorithms/discard.h"
#include "algorithms/index.h"
#include "algorithms/move.h"
#include "algorithms/union.h"
#include "algorithms/update.h"

// Core
#include "core/allocate.h"  // Allocator policies
#include "core/node.h"  // Nodes
#include "core/view.h"  // Views

// List
#include "list.h"  // VariantList


//////////////////////
////    PUBLIC    ////
//////////////////////


class VariantSet : public VariantList {
public:
    using Base = VariantList;

    /* Construct a new VariantSet from an existing SetView.  This is called to
    construct a new `VariantSet` from the output of `SetView.copy()` or
    `get_slice()`. */
    template <typename View>
    VariantSet(View&& view) : Base(view), self(nullptr) {}

    /* Construct an empty SetView to match the given template parameters.  This
    is called during `LinkedSet.__init__()` when no iterable is given. */
    VariantSet(bool doubly_linked, Py_ssize_t max_size, PyObject* spec) :
        self(nullptr)
    {
        if (doubly_linked) {
            if (max_size < 0) {
                this->variant = SetView<DoubleNode, DynamicAllocator>(max_size, spec);
            } else {
                this->variant = SetView<DoubleNode, FixedAllocator>(max_size, spec);
            }
        } else {
            if (max_size < 0) {
                this->variant = SetView<SingleNode, DynamicAllocator>(max_size, spec);
            } else {
                this->variant = SetView<SingleNode, FixedAllocator>(max_size, spec);
            }
        }
        this->_doubly_linked = doubly_linked;
    }

    /* Unpack an iterable into a new SetView and wrap it as a VariantSet.  This
    is called to construct a LinkedSet from an initializer sequence. */
    VariantSet(
        PyObject* iterable,
        bool doubly_linked,
        bool reverse,
        Py_ssize_t max_size,
        PyObject* spec
    ) : self(nullptr)
    {
        if (doubly_linked) {
            if (max_size < 0) {
                this->variant = SetView<DoubleNode, DynamicAllocator>(
                    iterable, reverse, max_size, spec
                );
            } else {
                this->variant = SetView<DoubleNode, FixedAllocator>(
                    iterable, reverse, max_size, spec
                );
            }
        } else {
            if (max_size < 0) {
                this->variant = SetView<SingleNode, DynamicAllocator>(
                    iterable, reverse, max_size, spec
                );
            } else {
                this->variant = SetView<SingleNode, FixedAllocator>(
                    iterable, reverse, max_size, spec
                );
            }
        }
        this->_doubly_linked = doubly_linked;
    }

    /////////////////////////////
    ////    SET INTERFACE    ////
    /////////////////////////////

    /* Dispatch to the correct implementation of add() for each variant. */
    inline void add(PyObject* item, bool left) {
        std::visit(
            [&](auto& view) {
                Ops::add(&view, item, left);
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of discard() for each variant. */
    inline void discard(PyObject* item) {
        std::visit(
            [&](auto& view) {
                Ops::discard(&view, item);
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of isdisjoint() for each variant. */
    inline int isdisjoint(PyObject* other) {
        return std::visit(
            [&](auto& view) {
                return Ops::isdisjoint(&view, other);
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of issubset() for each variant. */
    inline int issubset(PyObject* other, bool strict) {
        return std::visit(
            [&](auto& view) {
                return Ops::issubset(&view, other, strict);
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of issuperset() for each variant. */
    inline int issuperset(PyObject* other, bool strict) {
        return std::visit(
            [&](auto& view) {
                return Ops::issuperset(&view, other, strict);
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of union() for each variant. */
    inline VariantSet* union_(PyObject* other, bool left) {
        return std::visit(
            [&](auto& view) -> VariantSet* {
                auto result = Ops::union_(&view, other, left);
                if (result == nullptr) {
                    return nullptr;  // propagate Python errors
                }
                return new VariantSet(std::move(*result));
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of intersection() for each variant. */
    inline VariantSet* intersection(PyObject* other) {
        return std::visit(
            [&](auto& view) -> VariantSet* {
                auto result = Ops::intersection(&view, other);
                if (result == nullptr) {
                    return nullptr;  // propagate Python errors
                }
                return new VariantSet(std::move(*result));
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of difference() for each variant. */
    inline VariantSet* difference(PyObject* other) {
        return std::visit(
            [&](auto& view) -> VariantSet* {
                auto result = Ops::difference(&view, other);
                if (result == nullptr) {
                    return nullptr;  // propagate Python errors
                }
                return new VariantSet(std::move(*result));
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of symmetric_difference() for each
    variant. */
    inline VariantSet* symmetric_difference(PyObject* other) {
        return std::visit(
            [&](auto& view) -> VariantSet* {
                auto result = Ops::symmetric_difference(&view, other);
                if (result == nullptr) {
                    return nullptr;  // propagate Python errors
                }
                return new VariantSet(std::move(*result));
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of update() for each variant. */
    inline void update(PyObject* items, bool left) {
        std::visit(
            [&](auto& view) {
                Ops::update(&view, items, left);
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of intersection_update() for each
    variant. */
    inline void intersection_update(PyObject* other) {
        std::visit(
            [&](auto& view) {
                Ops::intersection_update(&view, other);
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of difference_update() for each
    variant. */
    inline void difference_update(PyObject* other) {
        std::visit(
            [&](auto& view) {
                Ops::difference_update(&view, other);
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of symmetric_difference_update() for
    each variant. */
    inline void symmetric_difference_update(PyObject* other) {
        std::visit(
            [&](auto& view) {
                Ops::symmetric_difference_update(&view, other);
            },
            this->variant
        );
    }

    ///////////////////////////////////
    ////    RELATIVE OPERATIONS    ////
    ///////////////////////////////////

    /* Dispatch to the correct implementation of edge() for each variant. */
    inline Py_ssize_t distance(PyObject* item1, PyObject* item2) {
        return std::visit(
            [&](auto& view) {
                return Ops::distance(&view, item1, item2);
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of swap() for each variant. */
    inline void swap(PyObject* item1, PyObject* item2) {
        std::visit(
            [&](auto& view) {
                Ops::swap(&view, item1, item2);
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of move() for each variant. */
    inline void move(PyObject* item, Py_ssize_t steps) {
        std::visit(
            [&](auto& view) {
                Ops::move(&view, item, steps);
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of move_to_index() for each variant. */
    template <typename T>
    inline void move_to_index(PyObject* item, T index) {
        std::visit(
            [&](auto& view) {
                Ops::move(&view, item, index);
            },
            this->variant
        );
    }

    /* A proxy object that weakly references a VariantSet and exposes additional
    methods for efficient operations with respect to a particular value. */
    class RelativeProxy {
    public:
        /* Disabled copy/move constructors.  These are potentially dangerous since
        we're using a raw PyObject* pointer. */
        RelativeProxy(const RelativeProxy&) = delete;
        RelativeProxy(RelativeProxy&&) = delete;
        RelativeProxy& operator=(const RelativeProxy&) = delete;
        RelativeProxy& operator=(RelativeProxy&&) = delete;

        /* Construct a proxy for the set that allows efficient operations relative
        to a particular sentinel value. */
        RelativeProxy(
            std::shared_ptr<VariantSet> variant,
            PyObject* sentinel,
            Py_ssize_t offset
        ) : variant(variant), sentinel(sentinel), steps(offset)
        {
            Py_INCREF(this->sentinel);
        }

        /* Decrement the reference count of the sentinel value on destruction. */
        ~RelativeProxy() {
            Py_DECREF(this->sentinel);
        }

        // TODO: can't return nullptr from offset().  Have to use std::optional or
        // something similar.

        /* Builder-pattern method for assigning an offset from the RelativeProxy's
        sentinel value at which to operate. */
        inline RelativeProxy offset(Py_ssize_t steps) {
            auto ref = strong_ref();
            if (ref == nullptr) {
                return nullptr;  // propagate
            }

            // return a new RelativeProxy with the updated offset
            return RelativeProxy(ref, sentinel, steps);
        }

        /* Dispatch to the correct implementation of get_relative() for each variant. */
        PyObject* get() {
            // get a strong reference to the VariantSet
            auto ref = strong_ref();
            if (ref == nullptr) {
                return nullptr;  // propagate
            }

            // dispatch to view.relative()
            return std::visit(
                [&](auto& view) {
                    return view.relative(sentinel, steps, Relative::get);
                },
                ref->variant
            );
        }

        /* Dispatch to the correct implementation of move_relative() for each variant. */
        void move(PyObject* value) {
            // get a strong reference to the VariantSet
            auto ref = strong_ref();
            if (ref == nullptr) {
                return;  // propagate
            }

            // dispatch to view.relative()
            std::visit(
                [&](auto& view) {
                    view.relative(sentinel, steps, Relative::move, value);
                },
                ref->variant
            );
        }

    private:
        const std::weak_ptr<VariantSet> variant;
        PyObject* const sentinel;
        const Py_ssize_t steps;

        /* Generate a strong reference to the VariantSet. */
        std::shared_ptr<VariantSet> strong_ref() {
            auto strong_ref = variant.lock();
            if (strong_ref == nullptr) {
                PyErr_SetString(
                    PyExc_ReferenceError,
                    "proxy references a set that no longer exists"
                );
            }
            return strong_ref;
        }
    };

    /* Construct a RelativeProxy for relative operations within a linked set. */
    inline RelativeProxy* relative(PyObject* sentinel, Py_ssize_t offset) {
        // NOTE: using an internal shared_ptr to allow the VariantSet to be weakly
        // referenced from the RelativeProxy.  If a set is destroyed while a
        // RelativeProxy is still referencing it, then the proxy will start raising
        // errors whenever it is accessed.
        if (self == nullptr) {
            self = std::make_shared<VariantSet>(*this);
        }

        // TODO: should probably return a new instance rather than using the new
        // keyword.

        return new RelativeProxy(self, sentinel, offset);
    }





    /* Dispatch to the correct implementation of move_relative() for each variant. */
    inline void move_relative(PyObject* item, PyObject* sentinel, Py_ssize_t offset) {
        std::visit(
            [&](auto& view) {
                Ops::move_relative(&view, item, sentinel, offset);
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of get_relative() for each variant. */
    inline PyObject* get_relative(PyObject* sentinel, Py_ssize_t offset) {
        return std::visit(
            [&](auto& view) {
                return Ops::get_relative(&view, sentinel, offset);
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of insert_relative() for each variant. */
    inline void insert_relative(PyObject* item, PyObject* sentinel, Py_ssize_t offset) {
        std::visit(
            [&](auto& view) {
                Ops::insert_relative(&view, item, sentinel, offset);
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of add_relative() for each variant. */
    inline void add_relative(PyObject* item, PyObject* sentinel, Py_ssize_t offset) {
        std::visit(
            [&](auto& view) {
                Ops::add_relative(&view, item, sentinel, offset);
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of extend_relative() for each variant. */
    inline void extend_relative(
        PyObject* items,
        PyObject* sentinel,
        Py_ssize_t offset,
        bool reverse
    ) {
        std::visit(
            [&](auto& view) {
                Ops::extend_relative(&view, items, sentinel, offset, reverse);
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of update_relative() for each variant. */
    inline void update_relative(
        PyObject* items,
        PyObject* sentinel,
        Py_ssize_t offset,
        bool reverse
    ) {
        std::visit(
            [&](auto& view) {
                Ops::update_relative(&view, items, sentinel, offset, reverse);
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of remove_relative() for each variant. */
    inline void remove_relative(PyObject* sentinel, Py_ssize_t offset) {
        std::visit(
            [&](auto& view) {
                Ops::remove_relative(&view, sentinel, offset);
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of discard_relative() for each variant. */
    inline void discard_relative(PyObject* sentinel, Py_ssize_t offset) {
        std::visit(
            [&](auto& view) {
                Ops::discard_relative(&view, sentinel, offset);
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of pop_relative() for each variant. */
    inline PyObject* pop_relative(PyObject* sentinel, Py_ssize_t offset) {
        return std::visit(
            [&](auto& view) {
                return Ops::pop_relative(&view, sentinel, offset);
            },
            this->variant
        );
    }

    /* Dispatch to the correct implementation of clear_relative() for each variant. */
    inline void clear_relative(
        PyObject* sentinel,
        Py_ssize_t offset,
        Py_ssize_t length
    ) {
        std::visit(
            [&](auto& view) {
                Ops::clear_relative(&view, sentinel, offset, length);
            },
            this->variant
        );
    }


private:
    std::shared_ptr<VariantSet> self;

};


#endif  // BERTRAND_STRUCTS_SET_H include guard
