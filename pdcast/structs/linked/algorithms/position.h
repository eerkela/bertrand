#ifndef BERTRAND_STRUCTS_LINKED_ALGORITHMS_POSITION_H
#define BERTRAND_STRUCTS_LINKED_ALGORITHMS_POSITION_H

#include <cstddef>  // size_t
#include <type_traits>  // std::enable_if_t<>
#include <Python.h>  // CPython API
#include "../../util/base.h"  // is_pyobject<>
#include "../../util/except.h"  // TypeError
#include "../../util/ops.h"  // lt(), ge(), plus()
#include "../core/view.h"  // ViewTraits, Direction, Yield


namespace bertrand {
namespace linked {


    ///////////////////////////////////
    ////    INDEX NORMALIZATION    ////
    ///////////////////////////////////


    /* Normalize a numeric index, applying Python-style wraparound and bounds
    checking. */
    inline size_t normalize_index(long long index, size_t size, bool truncate) {
        // wraparound
        bool lt_zero = index < 0;
        if (lt_zero) {
            index += size;
            lt_zero = index < 0;
        }

        // boundscheck
        if (lt_zero || index >= static_cast<long long>(size)) {
            if (truncate) {
                return lt_zero ? 0 : size - 1;
            }
            throw IndexError("list index out of range");
        }

        return static_cast<size_t>(index);
    }


    /* Normalize a Python integer for use as an index to a list. */
    inline size_t normalize_index(PyObject* index, size_t size, bool truncate) {
        if (!PyLong_Check(index)) {
            throw TypeError("index must be a Python integer");
        }

        // error can still occur if python int is too large to fit in long long
        long long value = PyLong_AsLongLong(index);
        if (value == -1 && PyErr_Occurred()) {
            throw catch_python();
        }
        return normalize_index(value, size, truncate);
    }


    /////////////////////
    ////    PROXY    ////
    /////////////////////


    /* A proxy for an element at a particular index of a linked data structure, as
    returned by the [] operator. */
    template <typename View, Yield yield, bool as_pytuple = false>
    class ElementProxy {

        template <Direction dir>
        using Iter = linked::Iterator<View, dir, yield, as_pytuple>;

        /* Infer result type based on `yield` parameter. */
        template <Yield Y = Yield::KEY, typename Dummy = void>
        struct DerefType {
            using type = typename View::Value;
        };
        template <typename Dummy>
        struct DerefType<Yield::VALUE, Dummy> {
            using type = typename View::MappedValue;
        };
        template <typename Dummy>
        struct DerefType<Yield::ITEM, Dummy> {
            using type = std::conditional_t<
                as_pytuple,
                python::Tuple<python::Ref::STEAL>,
                std::pair<typename View::Value, typename View::MappedValue>
            >;
        };

        using Deref = std::conditional_t<
            std::is_const_v<View>,
            const typename DerefType<yield>::type,
            typename DerefType<yield>::type
        >;

        union {
            Iter<Direction::FORWARD> fwd;
            Iter<Direction::BACKWARD> bwd;
        };
        bool is_fwd;
        View& view;

        template <Yield _yield, bool _as_pytuple, typename _View>
        friend auto position(_View& view, long long index)
            -> std::enable_if_t<
                ViewTraits<_View>::linked,
                std::conditional_t<
                    std::is_const_v<_View>,
                    const ElementProxy<_View, _yield, _as_pytuple>,
                    ElementProxy<_View, _yield, _as_pytuple>
                >
            >;

        template <Direction dir>
        ElementProxy(View& view, Iter<dir>&& it) : view(view) {
            if constexpr (dir == Direction::FORWARD) {
                is_fwd = true;
                new (&fwd) Iter<Direction::FORWARD>(std::move(it));
            } else {
                is_fwd = false;
                new (&bwd) Iter<Direction::BACKWARD>(std::move(it));
            }
        }

    public:
        ElementProxy(const ElementProxy&) = delete;
        ElementProxy(ElementProxy&&) = delete;
        ElementProxy& operator=(const ElementProxy&) = delete;
        ElementProxy& operator=(ElementProxy&&) = delete;

        /* Clean up the union iterator when the proxy is destroyed. */
        ~ElementProxy() {
            if (is_fwd) {
                fwd.~Iter<Direction::FORWARD>();
            } else {
                bwd.~Iter<Direction::BACKWARD>();
            }
        }

        /* Get the value at the current index. */
        inline Deref get() const {
            return is_fwd ? *fwd : *bwd;
        }

        /* Set the value at the current index. */
        template <typename... Args>
        inline void set(Args&&... args) {
            typename View::Node* node = view.node(std::forward<Args>(args)...);
            view.recycle(is_fwd ? fwd.replace(node) : bwd.replace(node));
        }

        /* Delete the value at the current index. */
        inline void del() {
            view.recycle(is_fwd ? fwd.drop() : bwd.drop());
        }

        /* Implicitly convert the proxy to the value where applicable.  This is
        syntactic sugar for the get() method, such that `Value value = list[i]` is
        equivalent to `Value value = list[i].get()`. */
        inline operator Deref() const {
            return get();
        }

        /* Assign the value at the current index.  This is syntactic sugar for the
        set() method, such that `list[i] = value` is equivalent to
        `list[i].set(value)`. */
        inline ElementProxy& operator=(const Deref& value) {
            set(value);
            return *this;
        }

    };


    /* Get a proxy for a value at a particular index of the list. */
    template <Yield yield = Yield::KEY, bool as_pytuple = false, typename View>
    inline auto position(View& view, long long index)
        -> std::enable_if_t<
            ViewTraits<View>::linked,
            std::conditional_t<
                std::is_const_v<View>,
                const ElementProxy<View, yield, as_pytuple>,
                ElementProxy<View, yield, as_pytuple>
            >
        >
    {
        size_t norm_index = normalize_index(index, view.size(), false);

        if constexpr (NodeTraits<typename View::Node>::has_prev) {
            if (view.closer_to_tail(norm_index)) {
                auto it = view.template rbegin<yield, as_pytuple>();
                for (size_t i = view.size() - 1; i > norm_index; --i, ++it);
                return ElementProxy<View, yield, as_pytuple>(view, std::move(it));
            }
        }

        auto it = view.template begin<yield, as_pytuple>();
        for (size_t i = 0; i < norm_index; ++i, ++it);
        return ElementProxy<View, yield, as_pytuple>(view, std::move(it));
    }


}  // namespace linked
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_ALGORITHMS_POSITION_H
