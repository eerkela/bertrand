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


    size_t normalize_index(long long index, size_t size, bool truncate) {
        if (index < 0) {
            index += size;
            if (index < 0) {
                if (truncate) {
                    return 0;
                }
                throw IndexError("list index out of range");
            }
        } else if (index >= static_cast<long long>(size)) {
            if (truncate) {
                return size - 1;
            }
            throw IndexError("list index out of range");
        }

        return static_cast<size_t>(index);
    }


    size_t normalize_index(PyObject* index, size_t size, bool truncate) {
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

        ~ElementProxy() {
            if (is_fwd) {
                fwd.~Iter<Direction::FORWARD>();
            } else {
                bwd.~Iter<Direction::BACKWARD>();
            }
        }

        inline Deref get() const {
            return is_fwd ? *fwd : *bwd;
        }

        template <typename... Args>
        inline void set(Args&&... args) {
            typename View::Node* node = view.node(std::forward<Args>(args)...);
            view.recycle(is_fwd ? fwd.replace(node) : bwd.replace(node));
        }

        inline void del() {
            view.recycle(is_fwd ? fwd.drop() : bwd.drop());
        }

        inline operator Deref() const {
            return get();
        }

        inline ElementProxy& operator=(const Deref& value) {
            set(value);
            return *this;
        }

    };


    template <Yield yield = Yield::KEY, bool as_pytuple = false, typename View>
    auto position(View& view, long long index)
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
