#ifndef BERTRAND_STRUCTS_LINKED_DICT_H
#define BERTRAND_STRUCTS_LINKED_DICT_H

#include <cstddef>  // size_t
#include <optional>  // std::optional
#include <ostream>  // std::ostream
#include <sstream>  // std::ostringstream
#include <string>  // std::string
#include <type_traits>  // std::conditional_t
#include <utility>  // std::pair
#include <variant>  // std::variant
#include <Python.h>  // CPython API
#include "../util/args.h"  // PyArgs
#include "../util/except.h"  // throw_python()
#include "../util/func.h"  // identity
#include "../util/ops.h"  // eq(), lexical_lt(), etc.
#include "core/allocate.h"  // Config
#include "core/view.h"  // DictView
#include "base.h"  // LinkedBase
#include "list.h"  // PyListInterface
#include "set.h"  // PySetInterface, PyLinkedSet

#include "algorithms/add.h"
#include "algorithms/contains.h"
#include "algorithms/count.h"
#include "algorithms/dict_compare.h"
#include "algorithms/discard.h"
#include "algorithms/distance.h"
#include "algorithms/get.h"
#include "algorithms/index.h"
#include "algorithms/insert.h"
#include "algorithms/map.h"
#include "algorithms/move.h"
#include "algorithms/pop.h"
#include "algorithms/position.h"
#include "algorithms/remove.h"
#include "algorithms/repr.h"
#include "algorithms/reverse.h"
#include "algorithms/rotate.h"
#include "algorithms/setdefault.h"
#include "algorithms/set_compare.h"
#include "algorithms/slice.h"
#include "algorithms/sort.h"
#include "algorithms/swap.h"
#include "algorithms/union.h"
#include "algorithms/update.h"


// TODO:
// >>> LinkedDict.fromkeys("abc")
// LinkedDict({"a": None, "b": None, "c": None})
// >>> quit()
// segmentation fault


namespace bertrand {
namespace linked {


template <typename K, typename V, unsigned int Flags, typename Lock>
class LinkedDict;
template <typename Dict>
class KeysProxy;
template <typename Dict>
class ValuesProxy;
template <typename Dict, bool as_pytuple>
class ItemsProxy;


namespace dict_config {

    template <typename Key, typename Value, unsigned int Flags>
    using NodeSelect = std::conditional_t<
        !!(Flags & Config::SINGLY_LINKED),
        Mapped<SingleNode<Key>, Value>,
        Mapped<DoubleNode<Key>, Value>
    >;

    template <typename T>
    struct IsDict : std::false_type {};
    template <typename _K, typename _V, unsigned int _Flags, typename _Lock>
    struct IsDict<LinkedDict<_K, _V, _Flags, _Lock>> : std::true_type {};

    template <typename T>
    static constexpr bool is_dict = IsDict<
        std::remove_cv_t<std::remove_reference_t<T>>
    >::value;

}


//////////////////////////
////    LINKEDDICT    ////
//////////////////////////


/* An ordered dictionary based on an integrated linked list and hash table. */
template <
    typename K,
    typename V,
    unsigned int Flags = Config::DEFAULT,
    typename Lock = BasicLock
>
class LinkedDict : public LinkedBase<
    linked::DictView<dict_config::NodeSelect<K, V, Flags>, Flags>,
    Lock
> {
    using Base = LinkedBase<
        linked::DictView<dict_config::NodeSelect<K, V, Flags>, Flags>,
        Lock
    >;
    using DynamicDict = LinkedDict<K, V, Flags & ~Config::FIXED_SIZE, Lock>;

    DynamicDict as_dynamic() const {
        DynamicDict result(this->size(), this->specialization());
        for (auto it = this->begin(), end = this->end(); it != end; ++it) {
            result.view.template node<Base::Allocator::INSERT_TAIL>(*(it.curr()));
        }
        return result;
    }

public:
    using View = typename Base::View;
    using Node = typename Base::Node;
    using Key = K;
    using Value = V;

    template <linked::Direction dir>
    using Iterator = typename Base::template Iterator<dir>;
    template <linked::Direction dir>
    using ConstIterator = typename Base::template ConstIterator<dir>;

    ////////////////////////////
    ////    CONSTRUCTORS    ////
    ////////////////////////////

    #if defined(__GNUC__) && !defined(__clang__)
        #pragma GCC diagnostic push
        #pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
    #elif defined(__clang__)
        #pragma clang diagnostic push
        #pragma clang diagnostic ignored "-Wuninitialized"
    #elif defined(_MSC_VER)
        #pragma warning(push)
        #pragma warning(disable: 26494)  // VAR_USE_BEFORE_INIT
        #pragma warning(disable: 26495)  // MEMBER_UNINIT
    #endif

    LinkedDict(
        std::optional<size_t> max_size = std::nullopt,
        PyObject* spec = nullptr
    ) : Base(
        [&] {
            if constexpr (DEBUG) {
                LOGGER.tag(LogTag::init);
                LOGGER.address(this);
                LOGGER("LinkedDict(", repr(max_size), ", ", repr(spec), ")");
                LOGGER.indent();
            }
            return max_size;
        }(),
        spec
    ) {
        if constexpr (DEBUG) {
            LOGGER.unindent();
        }
    }

    /* Construct a dictionary from an input iterable. */
    template <typename Container>
    LinkedDict(
        Container&& iterable,
        std::optional<size_t> max_size = std::nullopt,
        PyObject* spec = nullptr,
        bool reverse = false
    ) : Base([&] {
            if constexpr (DEBUG) {
                LOGGER.tag(LogTag::init);
                LOGGER.address(this);
                LOGGER(
                    "LinkedDict(", repr(iterable), ", ", repr(max_size), ", ",
                    repr(spec), ", ", reverse, ")"
                );
                LOGGER.indent();
            }
            if constexpr (dict_config::is_dict<Container>) {
                return iterable.view;
            } else {
                return std::forward<Container>(iterable);
            }
        }(),
        max_size,
        spec,
        reverse
    ) {
        if constexpr (DEBUG) {
            LOGGER.unindent();
        }
    }

    template <typename Iterator>
    LinkedDict(
        Iterator&& begin,
        Iterator&& end,
        std::optional<size_t> max_size = std::nullopt,
        PyObject* spec = nullptr,
        bool reverse = false
    ) : Base(
        [&] {
            if constexpr (DEBUG) {
                LOGGER.tag(LogTag::init);
                LOGGER.address(this);
                LOGGER(
                    "LinkedDict(", repr(begin), ", ", repr(end), ", ",
                    repr(max_size), ", ", repr(spec), ", ", reverse, ")"
                );
                LOGGER.indent();
            }
            return begin;
        }(),
        end,
        max_size,
        spec,
        reverse
    ) {
        if constexpr (DEBUG) {
            LOGGER.unindent();
        }
    }

    template <typename X>
    LinkedDict(
        std::initializer_list<X> init,
        std::optional<size_t> max_size = std::nullopt,
        PyObject* spec = nullptr,
        bool reverse = false
    ) : Base(
        [&] {
            if constexpr (DEBUG) {
                LOGGER.tag(LogTag::init);
                LOGGER.address(this);
                LOGGER(
                    "LinkedDict(", repr(init), ", ", repr(max_size), ", ",
                    repr(spec), ", ", reverse, ")"
                );
                LOGGER.indent();
            }
            return init;
        }(),
        max_size,
        spec,
        reverse
    ) {
        if constexpr (DEBUG) {
            LOGGER.unindent();
        }
    }

    LinkedDict(View&& view) : Base([&] {
        if constexpr (DEBUG) {
            LOGGER.tag(LogTag::init);
            LOGGER.address(this);
            LOGGER("LinkedDict(", repr(view), ")  # from view");
            LOGGER.indent();
        }
        return std::move(view);
    }()) {
        if constexpr (DEBUG) {
            LOGGER.unindent();
        }
    }

    LinkedDict(const LinkedDict& other) : Base([&] {
        if constexpr (DEBUG) {
            LOGGER.tag(LogTag::init);
            LOGGER.address(this);
            LOGGER("LinkedDict(", &other, ")  # copy");
            LOGGER.indent();
        }
        return other.view;
    }()) {
        if constexpr (DEBUG) {
            LOGGER.unindent();
        }
    }

    LinkedDict(LinkedDict&& other) : Base([&] {
        if constexpr (DEBUG) {
            LOGGER.tag(LogTag::init);
            LOGGER.address(this);
            LOGGER("LinkedDict(", &other, ")  # move");
            LOGGER.indent();
        }
        return std::move(other.view);
    }()) {
        if constexpr (DEBUG) {
            LOGGER.unindent();
        }
    }

    LinkedDict& operator=(const LinkedDict& other) {
        if (this == &other) {
            return *this;
        }
        LOG_CONTEXT(init, this, "LinkedDict = ", &other, "  # copy");
        Base::operator=(other);
        return *this;
    }

    LinkedDict& operator=(LinkedDict&& other) {
        if (this == &other) {
            return *this;
        }
        LOG_CONTEXT(init, this, "LinkedDict = ", &other, "  # move");
        Base::operator=(std::move(other));
        return *this;
    }

    ~LinkedDict() {
        if constexpr (DEBUG) {
            LOGGER.tag(LogTag::init);
            LOGGER.address(this);
            LOGGER("~LinkedDict");
            LOGGER.indent();  // indent to be closed in Allocator::~BaseAllocator()
        }
    }

    #if defined(__GNUC__) && !defined(__clang__)
        #pragma GCC diagnostic pop
    #elif defined(__clang__)
        #pragma clang diagnostic pop
    #elif defined(_MSC_VER)
        #pragma warning(pop)
    #endif

    //////////////////////////////
    ////    DICT INTERFACE    ////
    //////////////////////////////

    /* Create a new dictionary from a sequence of keys and a default value. */
    template <typename Container>
    static LinkedDict fromkeys(
        Container&& keys,
        const Value& value,
        std::optional<size_t> capacity = std::nullopt,
        PyObject* spec = nullptr
    ) {
        LOG_CONTEXT(
            init, nullptr, "LinkedDict::fromkeys(", repr(keys), ", ", repr(value), ")"
        );
        using Allocator = typename View::Allocator;
        static constexpr unsigned int flags = (
            Allocator::EXIST_OK | Allocator::REPLACE_MAPPED | Allocator::INSERT_TAIL
        );
        View view(capacity, spec);
        for (const auto& key : iter(keys)) {
            view.template node<flags>(key, value);
        }
        return LinkedDict(std::move(view));
    }

    /* Add a key-value pair to the end of the dictionary if it is not already
    present. */
    inline void add(const Key& key, const Value& value) {
        LOG_CONTEXT(call, this, "LinkedDict::add(", repr(key), ", ", repr(value), ")");
        linked::add(this->view, key, value);
    }

    /* Add a key-value pair to the beginning of the dictionary if it is not already
    present. */
    inline void add_left(const Key& key, const Value& value) {
        LOG_CONTEXT(
            call, this, "LinkedDict::add_left(", repr(key), ", ", repr(value), ")"
        );
        linked::add_left(this->view, key, value);
    }

    /* Add a key-value pair to the front of the dictionary, evicting the last item if
    necessary and moving items that are already present. */
    inline void lru_add(const Key& key, const Value& value) {
        LOG_CONTEXT(
            call, this, "LinkedDict::lru_add(", repr(key), ", ", repr(value), ")"
        );
        linked::lru_add(this->view, key, value);
    }

    /* Insert a key-value pair at a specific index of the dictionary. */
    inline void insert(long long index, const Key& key, const Value& value) {
        LOG_CONTEXT(
            call, this, "LinkedDict::insert(", index, ", ", repr(key), ", ",
            repr(value), ")"
        );
        linked::insert(this->view, index, key, value);
    }

    /* Get the index of a key within the dictionary. */
    inline size_t index(
        const Key& key,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        LOG_CONTEXT(
            call, this, "LinkedDict::index(", repr(key), ", ", repr(start), ", ",
            repr(stop), ")"
        );
        return linked::index(this->view, key, start, stop);
    }

    /* Count the number of occurrences of a key within the dictionary. */
    inline size_t count(
        const Key& key,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        LOG_CONTEXT(
            call, this, "LinkedDict::count(", repr(key), ", ", repr(start), ", ",
            repr(stop), ")"
        );
        return linked::count(this->view, key, start, stop);
    }

    /* Check if the dictionary contains a certain key. */
    inline bool contains(const Key& key) const {
        LOG_CONTEXT(call, this, "LinkedDict::contains(", repr(key), ")");
        return linked::contains(this->view, key);
    }

    /* Check if the dictionary contains a certain key and move it to the front of the
    dictionary if so. */
    inline bool lru_contains(const Key& key) {
        LOG_CONTEXT(call, this, "LinkedDict::lru_contains(", repr(key), ")");
        return linked::lru_contains(this->view, key);
    }

    /* Remove a key from the dictionary. */
    inline void remove(const Key& key) {
        LOG_CONTEXT(call, this, "LinkedDict::remove(", repr(key), ")");
        linked::remove(this->view, key);
    }

    /* Remove a key from the dictionary if it is present. */
    inline void discard(const Key& key) {
        LOG_CONTEXT(call, this, "LinkedDict::discard(", repr(key), ")");
        linked::discard(this->view, key);
    }

    /* Remove a key from the dictionary and return its value. */
    inline Value pop(const Key& key) {
        LOG_CONTEXT(call, this, "LinkedDict::pop(", repr(key), ")");
        return linked::pop(this->view, key);
    }

    /* Remove a key from the dictionary and return its value, or return an optional
    default if the key is not found. */
    inline Value pop(const Key& key, const Value& default_value) {
        LOG_CONTEXT(
            call, this, "LinkedDict::pop(", repr(key), ", ", repr(default_value), ")"
        );
        return linked::pop(this->view, key, default_value);
    }

    /* Remove and return a key, value pair from the dictionary. */
    inline std::pair<Key, Value> popitem(long long index = -1) {
        LOG_CONTEXT(call, this, "LinkedDict::popitem(", index, ")");
        return linked::popitem(this->view, index);
    }

    /* Remove all elements from the dictionary. */
    inline void clear() {
        LOG_CONTEXT(call, this, "LinkedDict::clear()");
        this->view.clear();
    }

    /* Look up a value in the dictionary. */
    inline Value& get(const Key& key) {
        LOG_CONTEXT(call, this, "LinkedDict::get(", repr(key), ")");
        return linked::get(this->view, key);
    }

    /* Look up a value in the dictionary. */
    inline Value& get(const Key& key) const {
        LOG_CONTEXT(call, this, "LinkedDict::get(", repr(key), ")");
        return linked::get(this->view, key);
    }

    /* Look up a value in the dictionary, returning a default value if it does not
    exist. */
    inline Value& get(const Key& key, Value& default_value) {
        LOG_CONTEXT(
            call, this, "LinkedDict::get(", repr(key), ", ", repr(default_value), ")"
        );
        return linked::get(this->view, key, default_value);
    }

    /* Look up a value in the dictionary, returning a default value if it does not
    exist. */
    inline Value& get(const Key& key, Value& default_value) const {
        LOG_CONTEXT(
            call, this, "LinkedDict::get(", repr(key), ", ", repr(default_value), ")"
        );
        return linked::get(this->view, key, default_value);
    }

    /* Look up a value in the dictionary and move it to the front if it is found. */
    inline Value& lru_get(const Key& key) {
        LOG_CONTEXT(call, this, "LinkedDict::lru_get(", repr(key), ")");
        return linked::lru_get(this->view, key);
    }

    /* Look up a value in the dictionary and move it to the front if it is found.
    Otherwise, return a default value. */
    inline Value& lru_get(const Key& key, Value& default_value) {
        LOG_CONTEXT(
            call, this, "LinkedDict::lru_get(", repr(key), ", ", repr(default_value),
            ")"
        );
        return linked::lru_get(this->view, key, default_value);
    }

    /* Set a value within the dictionary or insert it if it is not already present. */
    inline Value& setdefault(const Key& key, Value& default_value) {
        LOG_CONTEXT(
            call, this, "LinkedDict::setdefault(", repr(key), ", ",
            repr(default_value), ")"
        );
        return linked::setdefault(this->view, key, default_value);
    }

    /* Set a value within the dictionary or insert it at the front of the dictionary
    if it is not already present. */
    inline Value& setdefault_left(const Key& key, Value& default_value) {
        LOG_CONTEXT(
            call, this, "LinkedDict::setdefault_left(", repr(key), ", ",
            repr(default_value), ")"
        );
        return linked::setdefault_left(this->view, key, default_value);
    }

    /* Set a value within the dictionary and move it to the front of the dictionary,
    or insert it there if it is not already present.  Evicts the last element to make
    room if necessary. */
    inline Value& lru_setdefault(const Key& key, Value& default_value) {
        LOG_CONTEXT(
            call, this, "LinkedDict::lru_setdefault(", repr(key), ", ",
            repr(default_value), ")"
        );
        return linked::lru_setdefault(this->view, key, default_value);
    }

    /* Return a shallow copy of the dictionary. */
    inline LinkedDict copy() const {
        LOG_CONTEXT(call, this, "LinkedDict::copy()");
        return LinkedDict(this->view.copy());
    }

    /* Sort the keys within the dictionary in-place according to an optional key
    func. */
    template <typename Func>
    inline void sort(Func key = nullptr, bool reverse=false) {
        LOG_CONTEXT(
            call, this, "LinkedDict::sort(", repr(key), ", ", reverse, ")"
        );
        linked::sort<linked::MergeSort>(this->view, key, reverse);
    }

    /* Reverse the order of keys within the dictionary in-place. */
    inline void reverse() {
        LOG_CONTEXT(call, this, "LinkedDict::reverse()");
        linked::reverse(this->view);
    }

    /* Shift all keys in the dictionary to the right by the specified number of
    steps. */
    inline void rotate(long long steps = 1) {
        LOG_CONTEXT(call, this, "LinkedDict::rotate(", steps, ")");
        linked::rotate(this->view, steps);
    }

    /* Return a new dictionary with elements from this dictionary and all other
    containers. */
    inline DynamicDict union_() const {
        LOG_CONTEXT(call, this, "LinkedDict::union()");
        return as_dynamic();
    }

    /* Return a new dictionary with elements from this dictionary and all other
    containers. */
    template <typename First, typename... Rest>
    inline DynamicDict union_(First&& first, Rest&&... rest) const {
        LOG_CONTEXT(
            call, this, "LinkedDict::union(", repr(first), ", ... (+",
            sizeof...(Rest), "))"
        );

        auto execute = [&](auto&& arg) {
            DynamicDict result = linked::union_<Yield::ITEM, false>(
                this->view, std::forward<decltype(arg)>(arg)
            );
            if constexpr (sizeof...(Rest) > 0) {
                result.update(std::forward<Rest>(rest)...);
            }
            return result;
        };

        if constexpr (dict_config::is_dict<First>) {
            return execute(first.items());
        } else {
            return execute(std::forward<First>(first));
        }
    }

    /* Return a new dictionary with elements from this dictionary and all other
    containers.  Appends to the head of the dictionary rather than the tail. */
    inline DynamicDict union_left() const {
        LOG_CONTEXT(call, this, "LinkedDict::union_left()");
        return as_dynamic();
    }

    /* Return a new dictionary with elements from this dictionary and all other
    containers.  Appends to the head of the dictionary rather than the tail. */
    template <typename First, typename... Rest>
    inline DynamicDict union_left(First&& first, Rest&&... rest) const {
        LOG_CONTEXT(
            call, this, "LinkedDict::union_left(", repr(first), ", ... (+",
            sizeof...(Rest), "))"
        );
        auto execute = [&](auto&& arg) {
            DynamicDict result = linked::union_<Yield::ITEM, true>(
                this->view, std::forward<decltype(arg)>(arg)
            );
            if constexpr (sizeof...(Rest) > 0) {
                result.update_left(std::forward<Rest>(rest)...);
            }
            return result;
        };

        if constexpr (dict_config::is_dict<First>) {
            return execute(first.items());
        } else {
            return execute(std::forward<First>(first));
        }
    }

    /* Extend a dictionary by adding elements from one or more iterables that are not
    already present. */
    template <typename... Containers>
    inline void update(Containers&&... items) {
        LOG_CONTEXT(
            call, this, "LinkedDict::update(... (+", sizeof...(Containers), "))"
        );
        auto unwrap = [](auto&& arg) {
            using Arg = decltype(arg);
            if constexpr (dict_config::is_dict<Arg>) {
                return arg.items();
            } else {
                return std::forward<Arg>(arg);
            }
        };
        (
            linked::update<false>(
                this->view,
                unwrap(std::forward<Containers>(items))
            ),
            ...
        );
    }

    /* Extend a dictionary by left-adding elements from one or more iterables that are
    not already present. */
    template <typename... Containers>
    inline void update_left(Containers&&... items) {
        LOG_CONTEXT(
            call, this, "LinkedDict::update_left(... (+", sizeof...(Containers), "))"
        );
        auto unwrap = [](auto&& arg) {
            using Arg = decltype(arg);
            if constexpr (dict_config::is_dict<Arg>) {
                return arg.items();
            } else {
                return std::forward<Arg>(arg);
            }
        };
        (
            linked::update<true>(
                this->view,
                unwrap(std::forward<Containers>(items))
            ),
            ...
        );
    }

    /* Extend a dictionary by adding or moving items to the head of the dictionary and
    possibly evicting the tail to make room. */
    template <typename... Containers>
    inline void lru_update(Containers&&... items) {
        LOG_CONTEXT(
            call, this, "LinkedDict::lru_update(... (+", sizeof...(Containers), "))"
        );
        auto unwrap = [](auto&& arg) {
            using Arg = decltype(arg);
            if constexpr (dict_config::is_dict<Arg>) {
                return arg.items();
            } else {
                return std::forward<Arg>(arg);
            }
        };
        (
            linked::lru_update(
                this->view,
                unwrap(std::forward<Containers>(items))
            ),
            ...
        );
    }

    /* Return a new dictionary with elements common to this dictionary and all other
    containers. */
    inline DynamicDict intersection() const {
        LOG_CONTEXT(call, this, "LinkedDict::intersection()");
        return as_dynamic();
    }

    /* Return a new dictionary with elements common to this dictionary and all other
    containers. */
    template <typename First, typename... Rest>
    inline DynamicDict intersection(First&& first, Rest&&... rest) const {
        LOG_CONTEXT(
            call, this, "LinkedDict::intersection(", repr(first), ", ... (+",
            sizeof...(Rest), "))"
        );
        auto execute = [&](auto&& arg) {
            DynamicDict result = linked::intersection<Yield::ITEM>(
                this->view, std::forward<decltype(arg)>(arg)
            );
            if constexpr (sizeof...(Rest) > 0) {
                result.intersection_update(std::forward<Rest>(rest)...);
            }
            return result;
        };

        if constexpr (dict_config::is_dict<First>) {
            return execute(first.items());
        } else {
            return execute(std::forward<First>(first));
        }
    }

    /* Remove elements from a dictionary that are contained in one or more iterables. */
    template <typename... Containers>
    inline void intersection_update(Containers&&... items) {
        LOG_CONTEXT(
            call, this, "LinkedDict::intersection_update(... (+",
            sizeof...(Containers), "))"
        );
        auto unwrap = [](auto&& arg) {
            using Arg = decltype(arg);
            if constexpr (dict_config::is_dict<Arg>) {
                return arg.items();
            } else {
                return std::forward<Arg>(arg);
            }
        };
        (
            linked::intersection_update(
                this->view,
                unwrap(std::forward<Containers>(items))
            ),
            ...
        );
    }

    /* Return a new dictionary with elements from this dictionary that are common to
    any other containers. */
    inline DynamicDict difference() const {
        LOG_CONTEXT(call, this, "LinkedDict::difference()");
        return as_dynamic();
    }

    /* Return a new dictionary with elements from this dictionary that are common to
    any other containers. */
    template <typename First, typename... Rest>
    inline DynamicDict difference(First&& first, Rest&&... rest) const {
        LOG_CONTEXT(
            call, this, "LinkedDict::difference(", repr(first), ", ... (+",
            sizeof...(Rest), "))"
        );
        auto execute = [&](auto&& arg) {
            DynamicDict result = linked::difference<Yield::ITEM>(
                this->view, std::forward<decltype(arg)>(arg)
            );
            if constexpr (sizeof...(Rest) > 0) {
                result.difference_update(std::forward<Rest>(rest)...);
            }
            return result;
        };

        if constexpr (dict_config::is_dict<First>) {
            return execute(first.items());
        } else {
            return execute(std::forward<First>(first));
        }
    }

    /* Remove elements from a dictionary that are contained in one or more iterables. */
    template <typename... Containers>
    inline void difference_update(Containers&&... items) {
        LOG_CONTEXT(
            call, this, "LinkedDict::difference_update(... (+",
            sizeof...(Containers), "))"
        );
        auto unwrap = [](auto&& arg) {
            using Arg = decltype(arg);
            if constexpr (dict_config::is_dict<Arg>) {
                return arg.items();
            } else {
                return std::forward<Arg>(arg);
            }
        };
        (
            linked::difference_update(
                this->view,
                unwrap(std::forward<Containers>(items))
            ),
            ...
        );
    }

    /* Return a new dictionary with elements in either this dictionary or another
    container, but not both. */
    template <typename Container>
    inline DynamicDict symmetric_difference(Container&& items) const {
        LOG_CONTEXT(
            call, this, "LinkedDict::symmetric_difference(", repr(items), ")"
        );
        if constexpr (dict_config::is_dict<Container>) {
            return DynamicDict(
                linked::symmetric_difference<Yield::ITEM, false>(
                    this->view, items.items()
                )
            );
        } else {
            return DynamicDict(
                linked::symmetric_difference<Yield::ITEM, false>(
                    this->view, std::forward<Container>(items)
                )
            );
        }
    }

    /* Return a new dictionary with elements in either this dictionary or another
    container, but not both.  Appends to the head of the dictionary rather than the
    tail. */
    template <typename Container>
    inline DynamicDict symmetric_difference_left(Container&& items) const {
        LOG_CONTEXT(
            call, this, "LinkedDict::symmetric_difference_left(", repr(items), ")"
        );
        if constexpr (dict_config::is_dict<Container>) {
            return DynamicDict(
                linked::symmetric_difference<Yield::ITEM, true>(
                    this->view, items.items()
                )
            );
        } else {
            return DynamicDict(
                linked::symmetric_difference<Yield::ITEM, true>(
                    this->view, std::forward<Container>(items)
                )
            );
        }
    }

    /* Update a dictionary, keeping only elements found in either the dictionary or the
    given container, but not both. */
    template <typename Container>
    inline void symmetric_difference_update(Container&& items) {
        LOG_CONTEXT(
            call, this, "LinkedDict::symmetric_difference_update(", repr(items), ")"
        );
        linked::symmetric_difference_update<false>(
            this->view, std::forward<Container>(items)
        );
    }

    /* Update a dictionary, keeping only elements found in either the dictionary or the
    given container, but not both.  Appends to the head of the dictionary rather than
    the tail. */
    template <typename Container>
    inline void symmetric_difference_update_left(Container&& items) {
        LOG_CONTEXT(
            call, this, "LinkedDict::symmetric_difference_update_left(",
            repr(items), ")"
        );
        linked::symmetric_difference_update<true>(
            this->view, std::forward<Container>(items)
        );
    }

    /* Get the linear distance between two keys within the dictionary. */
    inline long long distance(const Key& from, const Key& to) const {
        LOG_CONTEXT(
            call, this, "LinkedDict::distance(", repr(from), ", ", repr(to), ")"
        );
        return linked::distance(this->view, from, to);
    }

    /* Swap the positions of two keys within the dictionary. */
    inline void swap(const Key& key1, const Key& key2) {
        LOG_CONTEXT(
            call, this, "LinkedDict::swap(", repr(key1), ", ", repr(key2), ")"
        );
        linked::swap(this->view, key1, key2);
    }

    /* Move a key within the dictionary by the specified number of steps. */
    inline void move(const Key& key, long long steps) {
        LOG_CONTEXT(call, this, "LinkedDict::move(", repr(key), ", ", steps, ")");
        linked::move(this->view, key, steps);
    }

    /* Move a key within the dictionary to a specific index. */
    inline void move_to_index(const Key& key, long long index) {
        LOG_CONTEXT(
            call, this, "LinkedDict::move_to_index(", repr(key), ", ", index, ")"
        );
        linked::move_to_index(this->view, key, index);
    }

    ///////////////////////
    ////    PROXIES    ////
    ///////////////////////

    /* Proxies allow access to particular elements or slices of a dictionary, allowing
     * for convenient, Python-like syntax for dictionary operations.
     *
     * MapProxies are returned by the normal index operator [] when given a possible
     * key within the dictionary.  The key does not have to be present, and merely
     * obtaining a proxy will not modify the dictionary in any way (unlike
     * std::unordered_map).  The proxy itself delays searches until one of its methods
     * is called, as follows:
     *
     *      Value get(): search the dictionary for this key and return its value.
     *      Value lru_get(): search for this key and move it to the front of the
     *          dictionary.
     *      void set(Value& value): set the value for this key or insert at the end of
     *          the dictionary if it is not already present.
     *      void set_left(Value& value): set the value for this key or insert at the
     *          front of the dictionary if it is not already present.
     *      void lru_set(Value& value): set the value for this key and move it to the
     *          front of the dictionary, evicting the last item to make room if
     *          necessary.
     *      void del(): remove this key from the dictionary.
     *      void operator=(Value& value): syntactic sugar for set(value).
     *      operator Value(): implicit conversion operator.  Syntactic sugar for get().
     *
     * KeysProxies, ValuesProxies, and ItemsProxies are returned by the keys(),
     * values(), and items() methods, respectively.  They follow the same semantics as
     * Python's built-in dict views, and can be used to iterate over and compare the
     * keys and values within the dictionary.  See the comments below for details on
     * how they work.
     *
     * Finally, ElementProxies and SliceProxies work the same as for lists, allowing
     * positional access to elements within the dictionary.  See the corresponding
     * documentation in linked/list.h for more information.
     */

    inline auto keys() const -> KeysProxy<LinkedDict> {
        LOG_CONTEXT(call, this, "LinkedDict::keys()");
        return KeysProxy<LinkedDict>(*this);
    }

    inline auto values() const -> ValuesProxy<LinkedDict> {
        LOG_CONTEXT(call, this, "LinkedDict::values()");
        return ValuesProxy<LinkedDict>(*this);
    }

    template <bool as_pytuple = false>
    inline auto items() const -> ItemsProxy<LinkedDict, as_pytuple> {
        LOG_CONTEXT(call, this, "LinkedDict::items()");
        return ItemsProxy<LinkedDict, as_pytuple>(*this);
    }

    inline auto map(const Key& key) -> linked::MapProxy<View> {
        LOG_CONTEXT(call, this, "LinkedDict::map(", repr(key), ")");
        return linked::map(this->view, key);
    }

    inline auto map(const Key& key) const -> const linked::MapProxy<const View> {
        LOG_CONTEXT(call, this, "LinkedDict::map(", repr(key), ")");
        return linked::map(this->view, key);
    }

    inline auto position(long long index) -> linked::ElementProxy<View, Yield::ITEM> {
        LOG_CONTEXT(call, this, "LinkedDict::position(", index, ")");
        return linked::position<Yield::ITEM>(this->view, index);
    }

    inline auto position(long long index) const
        -> const linked::ElementProxy<const View, Yield::ITEM>
    {
        LOG_CONTEXT(call, this, "LinkedDict::position(", index, ")");
        return linked::position<Yield::ITEM>(this->view, index);
    }

    inline linked::SliceProxy<View, DynamicDict, Yield::KEY> slice(
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt,
        std::optional<long long> step = std::nullopt
    ) {
        LOG_CONTEXT(
            call, this, "LinkedDict::slice(", repr(start), ", ", repr(stop), ", ",
            repr(step), ")"
        );
        return linked::slice<DynamicDict, Yield::KEY>(
            this->view, start, stop, step
        );
    }

    inline linked::SliceProxy<View, DynamicDict, Yield::KEY> slice(
        PyObject* py_slice
    ) {
        LOG_CONTEXT(call, this, "LinkedDict::slice(", repr(py_slice), ")");
        return linked::slice<DynamicDict, Yield::KEY>(this->view, py_slice);
    }

    inline const linked::SliceProxy<const View, DynamicDict, Yield::KEY> slice(
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt,
        std::optional<long long> step = std::nullopt
    ) const {
        LOG_CONTEXT(
            call, this, "LinkedDict::slice(", repr(start), ", ", repr(stop), ", ",
            repr(step), ")"
        );
        return linked::slice<DynamicDict, Yield::KEY>(
            this->view, start, stop, step
        );
    }

    inline const linked::SliceProxy<const View, DynamicDict, Yield::KEY> slice(
        PyObject* py_slice
    ) const {
        LOG_CONTEXT(call, this, "LinkedDict::slice(", repr(py_slice), ")");
        return linked::slice<DynamicDict, Yield::KEY>(this->view, py_slice);
    }

    //////////////////////////////////
    ////    OPERATOR OVERLOADS    ////
    //////////////////////////////////

    /* NOTE: The supported operators are as follows:
     *      (|, |=)     union, union update
     *      (&, &=)     intersection, intersection update
     *      (-, -=)     difference, difference update
     *      (^, ^=)     symmetric difference, symmetric difference update
     *      (==)        equality
     *      (!=)        inequality
     *      (<<)        string stream representation (equivalent to Python repr())
     *
     * These all work similarly to their Python counterparts except that they can
     * accept any iterable container in either C++ or Python as the other operand, and
     * cover a wider range of operations than the Python dict operators.  Intersections
     * and differences, in particular, are not normally supported by Python dicts, but
     * are implemented here for consistency with the set interface.  They can be used
     * to easily filter dictionaries based on a set of keys:
     *
     *      >>> d = LinkedDict({"a": 1, "b": 2, "c": 3, "d": 4})
     *      >>> d - {"a", "b"}
     *      LinkedDict({"c": 3, "d": 4})
     *      >>> d & {"a", "b"}
     *      LinkedDict({"a": 1, "b": 2})
     *
     * Note that if a mapping is provided to either of these operators, only the keys
     * will be used for comparison.
     *
     * The symmetric difference operator (^) is also supported, but only if it is
     * given another mapping type as the other operand.  In this case, it will remove
     * those keys that are common to both mappings and insert those that are unique
     * with their corresponding values.  For example:
     *
     *      >>> d = LinkedDict({"a": 1, "b": 2, "c": 3, "d": 4})
     *      >>> d ^ {"a": 1, "b": 2, "e": 5}
     *      LinkedDict({"c": 3, "d": 4, "e": 5})
     *
     * Finally, set comparison operators (<=, <, >=, >) are not supported for the
     * dictionary itself, but are supported for the proxies returned by keys() and
     * potentially items() if the values are also hashable (in which case it acts as a
     * set of key-value pairs).  For example:
     *
     *      >>> d = LinkedDict({"a": 1, "b": 2, "c": 3, "d": 4})
     *      >>> d.keys() <= {"a", "b", "c", "d"}
     *      True
     *      >>> d.items() <= {("a", 1), ("b", 2), ("c", 3), ("d", 4)}
     *      True
     */

    inline linked::MapProxy<View> operator[](const Key& key) {
        LOG_CONTEXT(call, this, "LinkedDict[", repr(key), "]");
        return map(key);
    }

    inline const linked::MapProxy<const View> operator[](const Key& key) const {
        LOG_CONTEXT(call, this, "LinkedDict[", repr(key), "]");
        return map(key);
    }
    
    template <typename Map>
    inline DynamicDict operator|(const Map& other) {
        LOG_CONTEXT(call, this, "LinkedDict | ", repr(other));
        return union_(other);
    }

    template <typename Map>
    inline LinkedDict& operator|=(const Map& other) {
        LOG_CONTEXT(call, this, "LinkedDict |= ", repr(other));
        update(other);
        return *this;
    }

    template <typename Map>
    inline DynamicDict operator-(const Map& other) {
        LOG_CONTEXT(call, this, "LinkedDict - ", repr(other));
        return difference(other);
    }

    template <typename Map>
    inline LinkedDict& operator-=(const Map& other) {
        LOG_CONTEXT(call, this, "LinkedDict -= ", repr(other));
        difference_update(other);
        return *this;
    }

    template <typename Map>
    inline DynamicDict operator&(const Map& other) {
        LOG_CONTEXT(call, this, "LinkedDict & ", repr(other));
        return intersection(other);
    }

    template <typename Map>
    inline LinkedDict& operator&=(const Map& other) {
        LOG_CONTEXT(call, this, "LinkedDict &= ", repr(other));
        intersection_update(other);
        return *this;
    }

    template <typename Map>
    inline DynamicDict operator^(const Map& other) {
        LOG_CONTEXT(call, this, "LinkedDict ^ ", repr(other));
        return symmetric_difference(other);
    }

    template <typename Map>
    inline LinkedDict& operator^=(const Map& other) {
        LOG_CONTEXT(call, this, "LinkedDict ^= ", repr(other));
        symmetric_difference_update(other);
        return *this;
    }

    template <typename Map>
    inline bool operator==(const Map& other) {
        LOG_CONTEXT(call, this, "LinkedDict == ", repr(other));
        using C = std::remove_cv_t<std::remove_reference_t<Map>>;
        if constexpr (std::is_same_v<C, LinkedDict>) {
            if (this == &other) {
                return true;
            }
        }

        if constexpr (dict_config::is_dict<Map>) {
            return linked::dict_equal<true>(this->view, other.items());
        } else {
            return linked::dict_equal<true>(this->view, other);
        }
    }

    template <typename Map>
    inline bool operator!=(const Map& other) {
        LOG_CONTEXT(call, this, "LinkedDict != ", repr(other));
        using C = std::remove_cv_t<std::remove_reference_t<Map>>;
        if constexpr (std::is_same_v<C, LinkedDict>) {
            if (this == &other) {
                return false;
            }
        }

        if constexpr (dict_config::is_dict<Map>) {
            return linked::dict_equal<false>(this->view, other.items());
        } else {
            return linked::dict_equal<false>(this->view, other);
        }
    }

};


template <typename K, typename V, unsigned int Flags, typename... Ts>
inline auto operator<<(std::ostream& stream, const LinkedDict<K, V, Flags, Ts...>& dict)
    -> std::ostream&
{
    LOG_CONTEXT(call, &dict, "ostream << LinkedDict");
    stream << linked::build_repr<Yield::ITEM>(
        dict.view,
        "LinkedDict",
        "{",
        "}",
        64
    );
    return stream;
}


///////////////////////
////    PROXIES    ////
///////////////////////


/* A proxy for a linked dictionary that accesses specifically its keys, values, or
key-value pairs. */
template <typename Dict, typename Result, Yield yield>
class DictProxy {
protected:
    using View = typename Dict::View;
    using Element = std::conditional_t<
        yield == Yield::KEY,
        typename View::Value,
        std::conditional_t<
            yield == Yield::VALUE,
            typename View::MappedValue,
            std::pair<typename View::Value, typename View::MappedValue>
        >
    >;

    const Dict& dict;

    DictProxy(const Dict& dict) : dict(dict) {}

public:

    inline const Dict& mapping() const {
        return dict;
    }

    inline size_t size() const {
        return dict.size();
    }

    inline bool contains(const Element& key) const {
        return linked::contains<yield>(dict.view, key);
    }

    inline size_t index(
        const Element& key,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return linked::index<yield>(dict.view, key, start, stop);
    }

    inline size_t count(
        const Element& key,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return linked::count<yield>(dict.view, key, start, stop);
    }

    /////////////////////////
    ////    ITERATORS    ////
    /////////////////////////

    inline auto begin() const { return dict.view.template begin<yield>(); }
    inline auto end() const { return dict.view.template end<yield>(); }
    inline auto cbegin() const { return dict.view.template cbegin<yield>(); }
    inline auto cend() const { return dict.view.template cend<yield>(); }
    inline auto rbegin() const { return dict.view.template rbegin<yield>(); }
    inline auto rend() const { return dict.view.template rend<yield>(); }
    inline auto crbegin() const { return dict.view.template crbegin<yield>(); }
    inline auto crend() const { return dict.view.template crend<yield>(); }

    ////////////////////////
    ////    INDEXING    ////
    ////////////////////////

    template <typename... Args>
    inline auto slice(Args&&... args) const
        -> const linked::SliceProxy<const View, Result, yield>
    {
        return linked::slice<Result, yield>(dict.view, std::forward<Args>(args)...);
    }

    inline auto position(long long index) const
        -> const linked::ElementProxy<const View, yield>
    {
        return linked::position<yield>(dict.view, index);
    }

    inline auto operator[](long long index) const
        -> const linked::ElementProxy<const View, yield>
    {
        return position(index);
    }

};


//////////////////////
////    keys()    ////
//////////////////////


/* A read-only proxy for a dictionary's keys, in the same style as Python's
`dict.keys()` accessor. */
template <typename Dict>
class KeysProxy : public DictProxy<
    Dict,
    LinkedSet<typename Dict::Key, Dict::FLAGS & ~Config::FIXED_SIZE, typename Dict::Lock>,
    Yield::KEY
> {
    using Key = typename Dict::Key;
    using Set = LinkedSet<Key, Dict::FLAGS & ~Config::FIXED_SIZE, typename Dict::Lock>;
    using Base = DictProxy<Dict, Set, Yield::KEY>;

    friend Dict;

    KeysProxy(const Dict& dict) : Base(dict) {}

    inline Set as_set() const {
        return Set(*this, this->dict.size(), this->dict.specialization());
    }

public:

    template <typename Container>
    inline bool isdisjoint(Container&& items) const {
        return linked::isdisjoint(
            this->dict.view, std::forward<Container>(items)
        );
    }

    template <typename Container>
    inline bool issubset(Container&& items) const {
        return linked::issubset<false>(
            this->dict.view, std::forward<Container>(items)
        );
    }

    template <typename Container>
    inline bool issuperset(Container&& items) const {
        return linked::issuperset<false>(
            this->dict.view, std::forward<Container>(items)
        );
    }

    inline Set union_() const {
        return as_set();
    }

    template <typename First, typename... Rest>
    inline Set union_(First&& first, Rest&&... rest) const {
        Set result = linked::union_<Yield::KEY, false>(
            this->dict.view, std::forward<First>(first)
        );
        if constexpr (sizeof...(Rest) > 0) {
            result.update(std::forward<Rest>(rest)...);
        }
        return result;
    }

    inline Set union_left() const {
        return as_set();
    }

    template <typename First, typename... Rest>
    inline Set union_left(First&& first, Rest&&... rest) const {
        Set result = linked::union_<Yield::KEY, true>(
            this->dict.view, std::forward<First>(first)
        );
        if constexpr (sizeof...(Rest) > 0) {
            result.update_left(std::forward<Rest>(rest)...);
        }
        return result;
    }

    inline Set difference() const {
        return as_set();
    }

    template <typename First, typename... Rest>
    inline Set difference(First&& first, Rest&&... rest) const {
        Set result = linked::difference<Yield::KEY>(
            this->dict.view, std::forward<First>(first)
        );
        if constexpr (sizeof...(Rest) > 0) {
            result.difference_update(std::forward<Rest>(rest)...);
        }
        return result;
    }

    inline Set intersection() const {
        return as_set();
    }

    template <typename First, typename... Rest>
    inline Set intersection(First&& first, Rest&&... rest) const {
        Set result = linked::intersection<Yield::KEY>(
            this->dict.view, std::forward<First>(first)
        );
        if constexpr (sizeof...(Rest) > 0) {
            result.intersection_update(std::forward<Rest>(rest)...);
        }
        return result;
    }

    template <typename Container>
    inline Set symmetric_difference(Container&& items) const {
        return linked::symmetric_difference<Yield::KEY, false>(
            this->dict.view, std::forward<Container>(items)
        );
    }

    template <typename Container>
    inline Set symmetric_difference_left(Container&& items) const {
        return linked::symmetric_difference<Yield::KEY, true>(
            this->dict.view, std::forward<Container>(items)
        );
    }

    template <typename Container>
    inline Set operator|(const Container& other) const {
        return union_(other);
    }

    template <typename Container>
    inline Set operator-(const Container& other) const {
        return difference(other);
    }

    template <typename Container>
    inline Set operator&(const Container& other) const {
        return intersection(other);
    }

    template <typename Container>
    inline Set operator^(const Container& other) const {
        return symmetric_difference(other);
    }

    template <typename Container>
    inline bool operator<(const Container& other) const {
        return linked::issubset<true>(this->dict.view, other);
    }

    template <typename Container>
    inline bool operator<=(const Container& other) const {
        return linked::issubset<false>(this->dict.view, other);
    }

    template <typename Container>
    inline bool operator==(const Container& other) const {
        using C = std::remove_cv_t<std::remove_reference_t<Container>>;
        if constexpr (std::is_same_v<C, KeysProxy>) {
            if (this == &other) {
                return true;
            }
        }
        return linked::set_equal<true>(this->dict.view, other);
    }

    template <typename Container>
    inline bool operator!=(const Container& other) const {
        using C = std::remove_cv_t<std::remove_reference_t<Container>>;
        if constexpr (std::is_same_v<C, KeysProxy>) {
            if (this == &other) {
                return false;
            }
        }
        return linked::set_equal<false>(this->dict.view, other);
    }

    template <typename Container>
    inline bool operator>=(const Container& other) const {
        return linked::issuperset<false>(this->dict.view, other);
    }

    template <typename Container>
    inline bool operator>(const Container& other) const {
        return linked::issuperset<true>(this->dict.view, other);
    }

};


template <typename Dict>
inline std::ostream& operator<<(std::ostream& stream, const KeysProxy<Dict>& keys) {
    stream << linked::build_repr<Yield::KEY>(
        keys.mapping().view,
        "LinkedDict_keys",
        "{",
        "}",
        64
    );
    return stream;
}


////////////////////////
////    values()    ////
////////////////////////


/* A read-only proxy for a dictionary's values in the same style as Python's
`dict.values()` accessor. */
template <typename Dict>
class ValuesProxy : public DictProxy<
    Dict,
    LinkedList<typename Dict::Value, Dict::FLAGS & ~Config::FIXED_SIZE, typename Dict::Lock>,
    Yield::VALUE
> {
    using Value = typename Dict::Value;
    using List = LinkedList<Value, Dict::FLAGS & ~Config::FIXED_SIZE, typename Dict::Lock>;
    using Base = DictProxy<Dict, List, Yield::VALUE>;

    friend Dict;

    ValuesProxy(const Dict& dict) : Base(dict) {}

public:

    template <typename Container>
    inline List operator+(const Container& other) const {
        return List(linked::concatenate<Yield::VALUE>(
            this->dict.view,other
        ));
    }

    inline List operator*(long long repetitions) const {
        return List(linked::repeat<Yield::VALUE>(
            this->dict.view, repetitions
        ));
    }

    template <typename Container>
    inline bool operator<(const Container& other) const {
        return lexical_lt(*this, other);
    }

    template <typename Container>
    inline bool operator<=(const Container& other) const {
        return lexical_le(*this, other);
    }

    template <typename Container>
    inline bool operator==(const Container& other) const {
        return lexical_eq(*this, other);
    }

    template <typename Container>
    inline bool operator!=(const Container& other) const {
        return !lexical_eq(*this, other);
    }

    template <typename Container>
    inline bool operator>=(const Container& other) const {
        return lexical_ge(*this, other);
    }

    template <typename Container>
    inline bool operator>(const Container& other) const {
        return lexical_gt(*this, other);
    }

};


template <typename Dict>
inline std::ostream& operator<<(std::ostream& stream, const ValuesProxy<Dict>& values) {
    stream << linked::build_repr<Yield::VALUE>(
        values.mapping().view,
        "LinkedDict_values",
        "[",
        "]",
        64
    );
    return stream;
}


template <typename Dict> 
inline auto operator*(long long other, const ValuesProxy<Dict>& proxy) {
    return proxy * other;
}


///////////////////////
////    items()    ////
///////////////////////


/* A read-only proxy for a dictionary's items, in the same style as Python's
`dict.items()` accessor. */
template <typename Dict, bool as_pytuple>
class ItemsProxy : public DictProxy<
    Dict,
    LinkedList<
        std::conditional_t<
            as_pytuple, PyObject*, std::pair<typename Dict::Key, typename Dict::Value>
        >,
        Dict::FLAGS & ~Config::FIXED_SIZE,
        typename Dict::Lock
    >,
    Yield::ITEM
> {
    using View = typename Dict::View;
    using Key = typename Dict::Key;
    using Value = typename Dict::Value;
    using List = LinkedList<
        std::conditional_t<as_pytuple, PyObject*, std::pair<Key, Value>>,
        Dict::FLAGS & ~Config::FIXED_SIZE,
        typename Dict::Lock
    >;
    using Base = DictProxy<Dict, List, Yield::ITEM>;

    friend Dict;

    ItemsProxy(const Dict& dict) : Base(dict) {}

public:

    inline size_t index(
        const Key& key,
        const Value& value,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        std::pair<Key, Value> item(key, value);
        return linked::index<Yield::ITEM>(this->dict.view, item, start, stop);
    }

    inline size_t index(
        const std::pair<Key, Value> item,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return linked::index<Yield::ITEM>(this->dict.view, item, start, stop);
    }

    inline size_t index(
        const std::tuple<Key, Value>& item,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        std::pair<Key, Value> pair(std::get<0>(item), std::get<1>(item));
        return linked::index<Yield::ITEM>(this->dict.view, pair, start, stop);
    }

    inline size_t index(
        PyObject* item,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        if (!PyTuple_Check(item) || PyTuple_GET_SIZE(item) != 2) {
            throw TypeError("expected a tuple of size 2");
        }
        std::pair<Key, Value> pair(PyTuple_GET_ITEM(item, 0), PyTuple_GET_ITEM(item, 1));
        return linked::index<Yield::ITEM>(this->dict.view, pair, start, stop);
    }

    inline size_t count(
        const Key& key,
        const Value& value,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        std::pair<Key, Value> item(key, value);
        return linked::count<Yield::ITEM>(this->dict.view, item, start, stop);
    }

    inline size_t count(
        const std::pair<Key, Value> item,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        return linked::count<Yield::ITEM>(this->dict.view, item, start, stop);
    }

    inline size_t count(
        const std::tuple<Key, Value>& item,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        std::pair<Key, Value> pair(std::get<0>(item), std::get<1>(item));
        return linked::count<Yield::ITEM>(this->dict.view, pair, start, stop);
    }

    inline size_t count(
        PyObject* item,
        std::optional<long long> start = std::nullopt,
        std::optional<long long> stop = std::nullopt
    ) const {
        if (!PyTuple_Check(item) || PyTuple_GET_SIZE(item) != 2) {
            throw TypeError("expected a tuple of size 2");
        }
        std::pair<Key, Value> pair(PyTuple_GET_ITEM(item, 0), PyTuple_GET_ITEM(item, 1));
        return linked::count<Yield::ITEM>(this->dict.view, pair, start, stop);
    }

    inline bool contains(const Key& key, const Value& value) const {
        std::pair<Key, Value> item(key, value);
        return linked::contains<Yield::ITEM>(this->dict.view, item);
    }

    inline bool contains(const std::pair<Key, Value> item) const {
        return linked::contains<Yield::ITEM>(this->dict.view, item);
    }

    inline bool contains(const std::tuple<Key, Value>& item) const {
        std::pair<Key, Value> pair(std::get<0>(item), std::get<1>(item));
        return linked::contains<Yield::ITEM>(this->dict.view, pair);
    }

    inline bool contains(PyObject* item) const {
        if (!PyTuple_Check(item) || PyTuple_GET_SIZE(item) != 2) {
            throw TypeError("expected a tuple of size 2");
        }
        std::pair<Key, Value> pair(PyTuple_GET_ITEM(item, 0), PyTuple_GET_ITEM(item, 1));
        return linked::contains<Yield::ITEM>(this->dict.view, pair);
    }

    /////////////////////////
    ////    ITERATORS    ////
    /////////////////////////

    inline auto begin() const {
        return this->dict.view.template begin<Yield::ITEM, as_pytuple>();
    }
    inline auto end() const {
        return this->dict.view.template end<Yield::ITEM, as_pytuple>();
    }
    inline auto cbegin() const {
        return this->dict.view.template cbegin<Yield::ITEM, as_pytuple>();
    }
    inline auto cend() const {
        return this->dict.view.template cend<Yield::ITEM, as_pytuple>();
    }
    inline auto rbegin() const {
        return this->dict.view.template rbegin<Yield::ITEM, as_pytuple>();
    }
    inline auto rend() const {
        return this->dict.view.template rend<Yield::ITEM, as_pytuple>();
    }
    inline auto crbegin() const {
        return this->dict.view.template crbegin<Yield::ITEM, as_pytuple>();
    }
    inline auto crend() const {
        return this->dict.view.template crend<Yield::ITEM, as_pytuple>();
    }

    ////////////////////////
    ////    INDEXING    ////
    ////////////////////////

    template <typename... Args>
    inline auto slice(Args&&... args) const
        -> const linked::SliceProxy<const View, List, Yield::ITEM, as_pytuple>
    {
        return linked::slice<List, Yield::ITEM, as_pytuple>(
            this->dict.view, std::forward<Args>(args)...
        );
    }

    inline auto position(long long index) const
        -> const linked::ElementProxy<const View, Yield::ITEM, as_pytuple>
    {
        return linked::position<Yield::ITEM, as_pytuple>(this->dict.view, index);
    }

    inline auto operator[](long long index) const
        -> const linked::ElementProxy<const View, Yield::ITEM, as_pytuple>
    {
        return position(index);
    }

    /////////////////////////
    ////    OPERATORS    ////
    /////////////////////////
    
    template <typename Container>
    inline List operator+(const Container& other) const {
        if constexpr (dict_config::is_dict<Container>) {
            return List(linked::concatenate<Yield::ITEM, as_pytuple>(
                this->dict.view, other.items()
            ));
        }
        return List(linked::concatenate<Yield::ITEM, as_pytuple>(
            this->dict.view, other
        ));
    }

    inline List operator*(long long repetitions) const {
        return List(linked::repeat<Yield::ITEM, as_pytuple>(
            this->dict.view, repetitions
        ));
    }

    template <typename Container>
    inline bool operator<(const Container& other) const {
        if constexpr (dict_config::is_dict<Container>) {
            return linked::itemsproxy_less<true>(
                this->dict.view, other.items()
            );
        }
        return linked::itemsproxy_less<true>(this->dict.view, other);
    }

    template <typename Container>
    inline bool operator<=(const Container& other) const {
        if constexpr (dict_config::is_dict<Container>) {
            return linked::itemsproxy_less<false>(
                this->dict.view, other.items()
            );
        }
        return linked::itemsproxy_less<false>(this->dict.view, other);
    }

    template <typename Container>
    inline bool operator==(const Container& other) const {
        using C = std::remove_cv_t<std::remove_reference_t<Container>>;
        if constexpr (dict_config::is_dict<Container>) {
            return linked::itemsproxy_equal(this->dict.view, other.items());
        } else if constexpr (std::is_same_v<C, ItemsProxy>) {
            if (this == &other) {
                return true;
            }
        }
        return linked::itemsproxy_equal(this->dict.view, other);
    }

    template <typename Container>
    inline bool operator!=(const Container& other) const {
        using C = std::remove_cv_t<std::remove_reference_t<Container>>;
        if constexpr (dict_config::is_dict<Container>) {
            return !linked::itemsproxy_equal(this->dict.view, other.items());
        } else if constexpr (std::is_same_v<C, ItemsProxy>) {
            if (this == &other) {
                return false;
            }
        }
        return !linked::itemsproxy_equal(this->dict.view, other);
    }

    template <typename Container>
    inline bool operator>=(const Container& other) const {
        if constexpr (dict_config::is_dict<Container>) {
            return linked::itemsproxy_greater<false>(
                this->dict.view, other.items()
            );
        }
        return linked::itemsproxy_greater<false>(this->dict.view, other);
    }

    template <typename Container>
    inline bool operator>(const Container& other) const {
        if constexpr (dict_config::is_dict<Container>) {
            return linked::itemsproxy_greater<true>(
                this->dict.view, other.items()
            );
        }
        return linked::itemsproxy_greater<true>(this->dict.view, other);
    }

};


template <typename Dict, bool as_pytuple>
inline auto operator<<(std::ostream& stream, const ItemsProxy<Dict, as_pytuple>& values)
    -> std::ostream&
{
    stream << linked::build_repr<Yield::ITEM>(
        values.mapping().view,
        "LinkedDict_items",
        "[",
        "]",
        64,
        "(",
        ", ",
        ")"
    );
    return stream;
}


template <typename Dict, bool as_pytuple> 
inline auto operator*(long long other, const ItemsProxy<Dict, as_pytuple>& proxy) {
    return proxy * other;
}


//////////////////////////////
////    PYTHON WRAPPER    ////
//////////////////////////////


/* CRTP mixin class containing the public dict interface for a linked data structure. */
template <typename Derived>
class PyDictInterface {

    template <typename Func, typename Result = PyObject*>
    static Result visit(Derived* self, Func func, Result err_code = nullptr) {
        try {
            return std::visit(func, self->variant);
        } catch (...) {
            throw_python();
            return err_code;
        }
    }

    template <typename Func>
    static auto unwrap_python(PyObject* arg, Func func) {
        if (Derived::typecheck(arg)) {
            return std::visit(func, reinterpret_cast<Derived*>(arg)->variant);
        }
        return func(arg);
    }

public:

    static PyObject* add(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        static constexpr std::string_view meth_name{"add"};
        return visit(self, [&args, &nargs](auto& dict) {
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* key = pyargs.parse("key");
            PyObject* value = pyargs.parse("value");
            pyargs.finalize();
            dict.add(key, value);
            Py_RETURN_NONE;
        });
    }

    static PyObject* add_left(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        static constexpr std::string_view meth_name{"add_left"};
        return visit(self, [&args, &nargs](auto& dict) {
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* key = pyargs.parse("key");
            PyObject* value = pyargs.parse("value");
            pyargs.finalize();
            dict.add_left(key, value);
            Py_RETURN_NONE;
        });
    }

    static PyObject* lru_add(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        static constexpr std::string_view meth_name{"lru_add"};
        return visit(self, [&args, &nargs](auto& dict) {
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* key = pyargs.parse("key");
            PyObject* value = pyargs.parse("value");
            pyargs.finalize();
            dict.lru_add(key, value);
            Py_RETURN_NONE;
        });
    }

    static PyObject* insert(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        static constexpr std::string_view meth_name{"distance"};
        using bertrand::util::parse_int;
        return visit(self, [&args, &nargs](auto& dict) {
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            long long index = pyargs.parse("index", parse_int);
            PyObject* key = pyargs.parse("key");
            PyObject* value = pyargs.parse("value");
            pyargs.finalize();
            dict.insert(index, key, value);
            Py_RETURN_NONE;
        });
    }

    static PyObject* pop(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        static constexpr std::string_view meth_name{"pop"};
        return visit(self, [&args, &nargs](auto& dict) {
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* key = pyargs.parse("key");
            PyObject* default_ = pyargs.parse("default", identity(), (PyObject*) nullptr);
            pyargs.finalize();
            if (default_ == nullptr) {
                return dict.pop(key);  // returns new reference
            } else {
                return dict.pop(key, default_);  // returns new reference
            }
        });
    }

    static PyObject* popitem(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        static constexpr std::string_view meth_name{"pop"};
        using bertrand::util::parse_int;
        return visit(self, [&args, &nargs](auto& dict) {
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            long long index = pyargs.parse("index", parse_int, (long long)-1);
            pyargs.finalize();
            std::pair<PyObject*, PyObject*> pair = dict.popitem(index);
            return PyTuple_Pack(2, pair.first, pair.second);
        });
    }

    static PyObject* get(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        static constexpr std::string_view meth_name{"get"};
        return visit(self, [&args, &nargs](auto& dict) {
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* key = pyargs.parse("key");
            PyObject* default_ = pyargs.parse("default", identity(), (PyObject*) nullptr);
            pyargs.finalize();
            if (default_ == nullptr) {
                return Py_NewRef(dict.get(key));
            } else {
                return Py_NewRef(dict.get(key, default_));
            }
        });
    }

    static PyObject* lru_get(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        static constexpr std::string_view meth_name{"lru_get"};
        return visit(self, [&args, &nargs](auto& dict) {
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* key = pyargs.parse("key");
            PyObject* default_ = pyargs.parse("default", identity(), (PyObject*) nullptr);
            pyargs.finalize();
            if (default_ == nullptr) {
                return Py_NewRef(dict.lru_get(key));
            } else {
                return Py_NewRef(dict.lru_get(key, default_));
            }
        });
    }

    static PyObject* setdefault(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        static constexpr std::string_view meth_name{"setdefault"};
        return visit(self, [&args, &nargs](auto& dict) {
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* key = pyargs.parse("key");
            PyObject* value = pyargs.parse("default", identity(), (PyObject*) nullptr);
            pyargs.finalize();
            return Py_NewRef(dict.setdefault(key, value));
        });
    }

    static PyObject* setdefault_left(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        static constexpr std::string_view meth_name{"setdefault_left"};
        return visit(self, [&args, &nargs](auto& dict) {
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* key = pyargs.parse("key");
            PyObject* value = pyargs.parse("default", identity(), (PyObject*) nullptr);
            pyargs.finalize();
            return Py_NewRef(dict.setdefault_left(key, value));
        });
    }

    static PyObject* lru_setdefault(Derived* self, PyObject* const* args, Py_ssize_t nargs) {
        static constexpr std::string_view meth_name{"lru_setdefault"};
        return visit(self, [&args, &nargs](auto& dict) {
            PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
            PyObject* key = pyargs.parse("key");
            PyObject* value = pyargs.parse("default", identity(), (PyObject*) nullptr);
            pyargs.finalize();
            return Py_NewRef(dict.lru_setdefault(key, value));
        });
    }

    static PyObject* keys(Derived* self, PyObject* = nullptr) {
        return visit(self, [&self](auto& dict) {
            using Proxy = std::decay_t<decltype(dict.keys())>;
            return PyKeysProxy<Proxy>::construct(self, dict.keys());
        });
    }

    static PyObject* values(Derived* self, PyObject* = nullptr) {
        return visit(self, [&self](auto& dict) {
            using Proxy = std::decay_t<decltype(dict.values())>;
            return PyValuesProxy<Proxy>::construct(self, dict.values());
        });
    }

    static PyObject* items(Derived* self, PyObject* = nullptr) {
        return visit(self, [&self](auto& dict) {
            using Proxy = std::decay_t<decltype(dict.template items<true>())>;
            return PyItemsProxy<Proxy>::construct(self, dict.template items<true>());
        });
    }

    static PyObject* __getitem__(Derived* self, PyObject* key) {
        return visit(self, [&key](auto& dict) {
            return Py_NewRef(dict[key].get());
        });
    }

    static int __setitem__(Derived* self, PyObject* key, PyObject* value) {
        return visit(self, [&key, &value](auto& dict) {
            if (value == nullptr) {
                dict[key].del();
            } else {
                dict[key] = value;
            }
            return 0;
        }, -1);
    }

    static PyObject* __richcompare__(Derived* self, PyObject* other, int cmp) {
        return visit(self, [&other, &cmp](auto& dict) {
            return unwrap_python(other, [&cmp, &dict](auto& other) {
                switch (cmp) {
                    case Py_EQ:
                        return Py_NewRef(dict == other ? Py_True : Py_False);
                    case Py_NE:
                        return Py_NewRef(dict != other ? Py_True : Py_False);
                    default:  // should never occur
                        PyErr_SetString(PyExc_TypeError, "invalid comparison");
                        return static_cast<PyObject*>(nullptr);
                }
            });
        });
    }

protected:

    /* Implement `PySequence_GetItem()` in CPython API. */
    static PyObject* __getitem_scalar__(Derived* self, Py_ssize_t index) {
        return visit(self, [&index](auto& dict) {
            std::pair<PyObject*, PyObject*> item = dict.position(index).get();
            return PyTuple_Pack(2, item.first, item.second);
        });
    }

    struct docs {

        static constexpr std::string_view add {R"doc(
Add a new key-value pair to the ``LinkedDict``.

Parameters
----------
key : Hashable
    The key to add.
value : Any
    The value to assign to ``key``.

Notes
-----
This is equivalent to ``LinkedDict[key] = value``, but is provided for
consistency with ``add_left()`` and ``lru_add()``.

Dict additions are always O(1).
)doc"
        };

        static constexpr std::string_view add_left {R"doc(
Add a new key-value pair to the left side of the ``LinkedDict``.

Parameters
----------
key : Hashable
    The key to add.
value : Any
    The value to assign to ``key``.

Notes
-----
Dict additions are always O(1).
)doc"
        };

        static constexpr std::string_view lru_add {R"doc(
Add a new key-value pair to the front of the ``LinkedDict`` or move it there if
it is already present.  Evicts the last element if the dictionary is full.

Parameters
----------
key : Hashable
    The key to add.
value : Any
    The value to assign to ``key``.

Notes
-----
LRU additions are O(1) for doubly-linked dictionaries and O(n) on average for
singly-linked ones if the key is already present.  This is due to the need to
traverse the dictionary in order to find the previous node.
)doc"
        };

        static constexpr std::string_view insert {R"doc(
Insert a new key-value pair into the ``LinkedDict`` at the specified index.

Parameters
----------
index : int
    The index at which to insert the new key-value pair.  This can be negative,
    following the same syntax as Python lists.
key : Hashable
    The key to insert.  Must be unique.
value : Any
    The value to insert.

Raises
------
KeyError
    If ``key`` is already present in the ``LinkedDict``.
)doc"
        };

        static constexpr std::string_view pop {R"doc(
Remove the specified key from the dictionary and return its value or an
optional default.

Parameters
----------
key : Hashable
    The key to remove.
default : Any, optional
    The value to return if ``key`` is not present in the ``LinkedDict``.  If
    not provided, a ``KeyError`` will be raised instead.

Returns
-------
Any
    The value associated with ``key`` if it was present in the ``LinkedDict``,
    otherwise ``default``.

Raises
------
KeyError
    If ``key`` is not present in the ``LinkedDict`` and ``default`` was not
    provided.

Notes
-----
Pops are O(1) for doubly-linked dictionaries and O(n) for singly-linked ones.
This is due to the need to traverse the entire dictionary in order to find the
previous node.
)doc"
        };

        static constexpr std::string_view popitem {R"doc(
Remove the key at the specified index and return its value.

Parameters
----------
index : int, default -1
    The index of the key to remove.  If not specified, the last key will be
    removed.  This can be negative, following the same syntax as Python lists.

Returns
-------
Any
    The value associated with the key at ``index``.

Raises
------
IndexError
    If the dictionary is empty or if ``index`` is out of bounds.

Notes
-----
``popitem()`` is analogous to :meth:`LinkedList.pop()`, and has the same
behavior.  It is consistent with the built-in :meth:`dict.popitem()` method,
except that it takes an optional index rather than always popping in LIFO
order.

Users should note that pops have different performance characteristics based on
whether they occur at the front or back of the dictionary.  Popping from the
front of a dictionary is O(1) for both singly- and doubly-linked dictionaries.
Popping from the back, however, is only O(1) for doubly-linked dictionaries.
It is O(n) for singly-linked dictionaries because the whole dictionary must be
traversed to find the new tail.

Pops towards the middle of the dictionary are O(n) in both cases.
)doc"
        };

        static constexpr std::string_view get {R"doc(
Return the value associated with a given key, or an optional default if it is
not present.

Parameters
----------
key : Hashable
    The key to look up.
default : Any, default None
    The value to return if ``key`` is not present in the ``LinkedDict``.

Returns
-------
Any
    The value associated with ``key`` if it was present in the ``LinkedDict``,
    otherwise ``default``.

Notes
-----
Gets are always O(1).
)doc"
        };

        static constexpr std::string_view lru_get {R"doc(
Return the value associated with a given key and move it to the front of the
dictionary if it is present.  Otherwise, return an optional default.

Parameters
----------
key : Hashable
    The key to look up.
default : Any, default None
    The value to return if ``key`` is not present in the ``LinkedDict``.

Returns
-------
Any
    The value associated with ``key`` if it was present in the ``LinkedDict``,
    otherwise ``default``.

Notes
-----
LRU gets are O(1) for doubly-linked dictionaries and O(n) on average for
singly-linked ones if the key is already prsent.  This is due to the need to
traverse the dictionary in order to find the previous node.
)doc"
        };

        static constexpr std::string_view setdefault {R"doc(
Get the value associated with a given key, or insert a new key-value pair if
it is not present.

Parameters
----------
key : Hashable
    The key to look up.
default : Any, default None
    The value to insert if ``key`` is not present in the dictionary.

Returns
-------
Any
    The value associated with ``key`` if it was present in the ``LinkedDict``,
    otherwise ``default``.

Notes
-----
``setdefault()`` is always O(1).
)doc"
        };

        static constexpr std::string_view setdefault_left {R"doc(
Get the value associated with a given key, or left-insert a new key-value pair
if it is not present.

Parameters
----------
key : Hashable
    The key to look up.
default : Any, default None
    The value to insert if ``key`` is not present in the dictionary.

Returns
-------
Any
    The value associated with ``key`` if it was present in the ``LinkedDict``,
    otherwise ``default``.

Notes
-----
``setdefault_left()`` is always O(1).
)doc"
        };

        static constexpr std::string_view lru_setdefault {R"doc(
Get the value associated with a given key and move it to the front of the
dictionary, or left-insert a new key-value pair and evict the tail node to make
room if necessary.

Parameters
----------
key : Hashable
    The key to look up.
default : Any, default None
    The value to insert if ``key`` is not present in the dictionary.

Returns
-------
Any
    The value associated with ``key`` if it was present in the ``LinkedDict``,
    otherwise ``default``.

Notes
-----
LRU setdefaults are O(1) for doubly-linked dictionaries and O(n) on average for
singly-linked ones if the key is already present.  This is due to the need to
traverse the dictionary in order to find the previous node.
)doc"
        };

        static constexpr std::string_view keys {R"doc(
Return a setlike, read-only proxy for the keys within the dictionary.

Returns
-------
KeysProxy
    A proxy for the keys within the dictionary.  This behaves like a read-only
    ``LinkedSet``, and supports many of the same operations.

Notes
-----
The proxy returned by this method can be iterated over, indexed, and compared
with other sets or setlike objects using the standard set operators.
)doc"
        };

        static constexpr std::string_view values {R"doc(
Return a listlike, read-only proxy for the values within the dictionary.

Returns
-------
ValuesProxy
    A proxy for the values within the dictionary.  This behaves like a read-only
    ``LinkedList``, and supports many of the same operations.

Notes
-----
The proxy returned by this method can be iterated over, indexed, and compared
with other lists or listlike objects using the standard list operators.
)doc"
        };

        static constexpr std::string_view items {R"doc(
Return a read-only proxy for the key-value pairs within the dictionary.

Returns
-------
ItemsProxy
    A proxy for the key-value pairs within the dictionary.  This behaves like a
    read-only set of pairs.

Notes
-----
The proxy returned by this method can be iterated over and indexed like a list.
It also supports setlike comparisons with other sets or setlike objects if and
only if the dictionary's values are also hashable.
)doc"
        };

    };

    /* Base class for keys(), values(), and items() proxies. */
    template <typename PyProxy, typename CppProxy>
    class PyDictProxy {
        PyObject_HEAD
        CppProxy proxy;
        PyObject* _mapping;

    protected:

        template <typename Func, typename Result = PyObject*>
        static Result visit(PyProxy* self, Func func, Result err_code = nullptr) {
            try {
                return func(self->proxy);
            } catch (...) {
                throw_python();
                return err_code;
            }
        }

        template <typename Func>
        static auto unwrap_python(PyObject* arg, Func func) {
            if (Derived::typecheck(arg)) {
                return std::visit(
                    func,
                    reinterpret_cast<Derived*>(arg)->variant
                );
            } else if (PyProxy::typecheck(arg)) {
                return func(reinterpret_cast<PyProxy*>(arg)->proxy);
            }
            return func(arg);
        }

    public:
        PyDictProxy() = delete;
        PyDictProxy(const PyDictProxy&) = delete;
        PyDictProxy(PyDictProxy&&) = delete;
        PyDictProxy& operator=(const PyDictProxy&) = delete;
        PyDictProxy& operator=(PyDictProxy&&) = delete;

        inline static PyObject* mapping(PyProxy* self, PyObject* = nullptr) noexcept {
            return Py_NewRef(self->_mapping);
        }

        static PyObject* index(PyProxy* self, PyObject* const* args, Py_ssize_t nargs) {
            static constexpr std::string_view meth_name{"index"};
            using bertrand::util::parse_opt_int;
            using Index = std::optional<long long>;
            return visit(self, [&args, &nargs](auto& proxy) {
                PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
                PyObject* element = pyargs.parse("element");
                Index start = pyargs.parse("start", parse_opt_int, Index());
                Index stop = pyargs.parse("stop", parse_opt_int, Index());
                pyargs.finalize();
                return PyLong_FromSize_t(proxy.index(element, start, stop));
            });
        }

        static PyObject* count(PyProxy* self, PyObject* const* args, Py_ssize_t nargs) {
            static constexpr std::string_view meth_name{"count"};
            using bertrand::util::parse_opt_int;
            using Index = std::optional<long long>;
            return visit(self, [&args, &nargs](auto& proxy) {
                PyArgs<CallProtocol::FASTCALL> pyargs(meth_name, args, nargs);
                PyObject* element = pyargs.parse("element");
                Index start = pyargs.parse("start", parse_opt_int, Index());
                Index stop = pyargs.parse("stop", parse_opt_int, Index());
                pyargs.finalize();
                return PyLong_FromSize_t(proxy.count(element, start, stop));
            });
        }

        inline static Py_ssize_t __len__(PyProxy* self) noexcept {
            return self->proxy.size();
        }

        inline static int __contains__(PyProxy* self, PyObject* item) {
            return visit(self, [&item](auto& proxy) {
                return proxy.contains(item);
            }, -1);
        }

        inline static PyObject* __iter__(PyProxy* self) noexcept {
            return visit(self, [&self](auto& proxy) {
                return iter(proxy).cpython(reinterpret_cast<PyObject*>(self));
            });
        }

        inline static PyObject* __reversed__(PyProxy* self, PyObject* = nullptr) noexcept {
            return visit(self, [&self](auto& proxy) {
                return iter(proxy).crpython(reinterpret_cast<PyObject*>(self));
            });
        }

        template <typename Result>
        static PyObject* __getitem__(PyProxy* self, PyObject* key) {
            using bertrand::util::parse_int;
            return visit(self, [&key](auto& proxy) {
                if (PyIndex_Check(key)) {
                    return Py_XNewRef(proxy[parse_int(key)].get());
                }

                if (PySlice_Check(key)) {
                    return Result::construct(proxy.slice(key).get());
                }

                PyErr_Format(
                    PyExc_TypeError,
                    "indices must be integers or slices, not %s",
                    Py_TYPE(key)->tp_name
                );
                return static_cast<PyObject*>(nullptr);
            });
        }

        static PyObject* __richcompare__(PyProxy* self, PyObject* other, int cmp) {
            return visit(self, [&other, &cmp](auto& proxy) {
                return unwrap_python(other, [&cmp, &proxy](auto& other) {
                    switch (cmp) {
                        case Py_LT:
                            return Py_NewRef(proxy < other ? Py_True : Py_False);
                        case Py_LE:
                            return Py_NewRef(proxy <= other ? Py_True : Py_False);
                        case Py_EQ:
                            return Py_NewRef(proxy == other ? Py_True : Py_False);
                        case Py_NE:
                            return Py_NewRef(proxy != other ? Py_True : Py_False);
                        case Py_GE:
                            return Py_NewRef(proxy >= other ? Py_True : Py_False);
                        case Py_GT:
                            return Py_NewRef(proxy > other ? Py_True : Py_False);
                        default:  // should never occur
                            PyErr_SetString(PyExc_TypeError, "invalid comparison");
                            return static_cast<PyObject*>(nullptr);
                    }
                });
            });
        }

        static PyObject* __repr__(PyProxy* self) {
            return visit(self, [](auto& proxy) {
                std::ostringstream stream;
                stream << proxy;
                auto str = stream.str();
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            });
        }

    protected:
        friend PyDictInterface;

        static PyObject* construct(Derived* dict, CppProxy&& proxy) {
            PyProxy* self = reinterpret_cast<PyProxy*>(
                PyProxy::Type.tp_alloc(&PyProxy::Type, 0)
            );
            if (self == nullptr) {
                PyErr_SetString(
                    PyExc_RuntimeError,
                    "failed to allocate memory for DictProxy"
                );
                return nullptr;
            }
            new (&self->proxy) CppProxy(std::move(proxy));
            self->_mapping = PyDictProxy_New(reinterpret_cast<PyObject*>(dict));
            return reinterpret_cast<PyObject*>(self);
        }

        static void __dealloc__(PyProxy* self) {
            Py_DECREF(self->_mapping);
            self->~PyProxy();
            PyProxy::Type.tp_free(reinterpret_cast<PyObject*>(self));
        }

        /* Implement `PySequence_GetItem()` in CPython API. */
        static PyObject* __getitem_scalar__(PyProxy* self, Py_ssize_t index) {
            return visit(self, [&index](auto& proxy) {
                return Py_XNewRef(proxy.position(index).get());
            });
        }

        struct docs {

            static constexpr std::string_view mapping {R"doc(
A reference to the ``LinkedDict`` that this proxy wraps.

Returns
-------
MappingProxy
    A read-only proxy for the ``LinkedDict`` that this proxy references.
)doc"
            };

            static constexpr std::string_view __reversed__ {R"doc(
Get a reverse iterator over the proxy.

Returns
-------
iter
    A reverse iterator over the proxy.

Notes
-----
This method is used by the built-in :func:`reversed() <python:reversed>`
function to iterate over the proxy in reverse order.

Note that reverse iteration has different performance characteristics for
doubly-linked lists vs singly-linked ones.  For the former, we can iterate in
either direction with equal efficiency, but for the latter, we must construct
a temporary stack to store the nodes in reverse order.  This can be expensive
in both time and memory, requiring two full iterations over the list rather
than one.
)doc"
            };

        };

    };

    /* Python wrapper for LinkedDict.keys(). */
    template <typename Proxy>
    class PyKeysProxy : public PyDictProxy<PyKeysProxy<Proxy>, Proxy> {
        friend PyDictInterface;
        using Base = PyDictProxy<PyKeysProxy, Proxy>;
        using IList = PyListInterface<Derived>;
        using ISet = PySetInterface<Derived>;

    public: 

        static PyObject* isdisjoint(PyKeysProxy* self, PyObject* other) {
            return Base::visit(self, [&other](auto& proxy) {
                return Base::unwrap_python(other, [&proxy](auto& other) {
                    return Py_NewRef(proxy.isdisjoint(other) ? Py_True : Py_False);
                });
            });
        }

        static PyObject* issubset(PyKeysProxy* self, PyObject* other) {
            return Base::visit(self, [&other](auto& proxy) {
                return Base::unwrap_python(other, [&proxy](auto& other) {
                    return Py_NewRef(proxy.issubset(other) ? Py_True : Py_False);
                });
            });
        }

        static PyObject* issuperset(PyKeysProxy* self, PyObject* other) {
            return Base::visit(self, [&other](auto& proxy) {
                return Base::unwrap_python(other, [&proxy](auto& other) {
                    return Py_NewRef(proxy.issuperset(other) ? Py_True : Py_False);
                });
            });
        }

        static PyObject* union_(
            PyKeysProxy* self,
            PyObject* const* args,
            Py_ssize_t nargs
        ) {
            return Base::visit(self, [&args, &nargs](auto& proxy) {
                auto result = proxy.union_();
                auto execute = [&result](auto& other) {
                    result.update(other);
                };
                for (Py_ssize_t i = 0; i < nargs; ++i) {
                    Base::unwrap_python(args[i], execute);
                }
                return PyLinkedSet::construct(std::move(result));
            });
        }

        static PyObject* union_left(
            PyKeysProxy* self,
            PyObject* const* args,
            Py_ssize_t nargs
        ) {
            return Base::visit(self, [&args, &nargs](auto& proxy) {
                auto result = proxy.union_left();
                auto execute = [&result](auto& other) {
                    result.update_left(other);
                };
                for (Py_ssize_t i = 0; i < nargs; ++i) {
                    Base::unwrap_python(args[i], execute);
                }
                return PyLinkedSet::construct(std::move(result));
            });
        }

        static PyObject* difference(
            PyKeysProxy* self,
            PyObject* const* args,
            Py_ssize_t nargs
        ) {
            return Base::visit(self, [&args, &nargs](auto& proxy) {
                auto result = proxy.difference(args[0]);
                auto execute = [&result](auto& other) {
                    result.difference_update(other);
                };
                for (Py_ssize_t i = 1; i < nargs; ++i) {
                    Base::unwrap_python(args[i], execute);
                }
                return PyLinkedSet::construct(std::move(result));
            });
        }

        static PyObject* intersection(
            PyKeysProxy* self,
            PyObject* const* args,
            Py_ssize_t nargs
        ) {
            return Base::visit(self, [&args, &nargs](auto& proxy) {
                auto result = proxy.intersection(args[0]);
                auto execute = [&result](auto& other) {
                    result.intersection_update(other);
                };
                for (Py_ssize_t i = 1; i < nargs; ++i) {
                    Base::unwrap_python(args[i], execute);
                }
                return PyLinkedSet::construct(std::move(result));
            });
        }

        static PyObject* symmetric_difference(PyKeysProxy* self, PyObject* other) {
            return Base::visit(self, [&other](auto& proxy) {
                return Base::unwrap_python(other, [&proxy](auto& other) {
                    return PyLinkedSet::construct(proxy.symmetric_difference(other));
                });
            });
        }

        static PyObject* symmetric_difference_left(PyKeysProxy* self, PyObject* other) {
            return Base::visit(self, [&other](auto& proxy) {
                return Base::unwrap_python(other, [&proxy](auto& other) {
                    return PyLinkedSet::construct(proxy.symmetric_difference_left(other));
                });
            });
        }

        static PyObject* __or__(PyKeysProxy* self, PyObject* other) {
            return Base::visit(self, [&other](auto& proxy) {
                return Base::unwrap_python(other, [&proxy](auto& other) {
                    return PyLinkedSet::construct(proxy | other);
                });
            });
        }

        static PyObject* __sub__(PyKeysProxy* self, PyObject* other) {
            return Base::visit(self, [&other](auto& proxy) {
                return Base::unwrap_python(other, [&proxy](auto& other) {
                    return PyLinkedSet::construct(proxy - other);
                });
            });
        }

        static PyObject* __and__(PyKeysProxy* self, PyObject* other) {
            return Base::visit(self, [&other](auto& proxy) {
                return Base::unwrap_python(other, [&proxy](auto& other) {
                    return PyLinkedSet::construct(proxy & other);
                });
            });
        }

        static PyObject* __xor__(PyKeysProxy* self, PyObject* other) {
            return Base::visit(self, [&other](auto& proxy) {
                return Base::unwrap_python(other, [&proxy](auto& other) {
                    return PyLinkedSet::construct(proxy ^ other);
                });
            });
        }

        static PyObject* __str__(PyKeysProxy* self) {
            return Base::visit(self, [](auto& proxy) {
                std::ostringstream stream;
                stream << "{";
                auto it = proxy.begin();
                auto end = proxy.end();
                if (it != end) {
                    stream << repr(*it);
                    ++it;
                }
                while (it != end) {
                    stream << ", " << repr(*it);
                    ++it;
                }
                stream << "}";
                auto str = stream.str();
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            });
        }

    private:

        struct docs {

            static constexpr std::string_view PyKeysProxy {R"doc(
A read-only, setlike proxy for the keys within a ``LinkedDict``.

Notes
-----
Instances of this class are returned by the :meth:`LinkedDict.keys()` accessor,
which behaves in the same way as the built-in :meth:`dict.keys()`.

These proxies support the following operations:

    #.  Iteration: ``len(keys)``, ``for key in keys: ...``, ``key in keys``
    #.  Indexing: ``keys[0]``, ``keys[1:3]``, ``keys.index(key)``,
        ``keys.count(key)``
    #.  Set methods: ``keys.union()``, ``keys.union_left()``,
        ``keys.intersection()``, ``keys.difference()``,
        ``keys.symmetric_difference()``, ``keys.symmetric_difference_left()``,
        ``keys.issubset()``, ``keys.issuperset()``, ``keys.isdisjoint()``
    #.  Set operators: ``keys | other``, ``keys - other``, ``keys & other``,
        ``keys ^ other``, ``keys < other``, ``keys <= other``,
        ``keys == other``, ``keys != other``, ``keys >= other``,
        ``keys > other``
)doc"
            };

        };

        inline static PyMappingMethods mapping_methods = [] {
            PyMappingMethods slots;
            slots.mp_length = (lenfunc) Base::__len__;
            slots.mp_subscript = (binaryfunc) Base::template __getitem__<PyLinkedSet>;
            return slots;
        }();

        inline static PySequenceMethods sequence = [] {
            PySequenceMethods slots;
            slots.sq_length = (lenfunc) Base::__len__;
            slots.sq_item = (ssizeargfunc) Base::__getitem_scalar__;
            slots.sq_contains = (objobjproc) Base::__contains__;
            return slots;
        }();

        inline static PyNumberMethods number = [] {
            PyNumberMethods slots;
            slots.nb_or = (binaryfunc) __or__;
            slots.nb_subtract = (binaryfunc) __sub__;
            slots.nb_and = (binaryfunc) __and__;
            slots.nb_xor = (binaryfunc) __xor__;
            return slots;
        }();

        inline static PyGetSetDef properties[] = {
            {"mapping", (getter) Base::mapping, nullptr, Base::docs::mapping.data()},
            {NULL}
        };

        inline static PyMethodDef methods[] = {
            {"index", (PyCFunction) Base::index, METH_FASTCALL, IList::docs::index.data()},
            {"count", (PyCFunction) Base::count, METH_FASTCALL, IList::docs::count.data()},
            {"isdisjoint", (PyCFunction) isdisjoint, METH_O, ISet::docs::isdisjoint.data()},
            {"issubset", (PyCFunction) issubset, METH_O, ISet::docs::issubset.data()},
            {"issuperset", (PyCFunction) issuperset, METH_O, ISet::docs::issuperset.data()},
            {"union", (PyCFunction) union_, METH_FASTCALL, ISet::docs::union_.data()},
            {
                "union_left",
                (PyCFunction) union_left,
                METH_FASTCALL,
                ISet::docs::union_left.data()
            },
            {
                "difference",
                (PyCFunction) difference,
                METH_FASTCALL,
                ISet::docs::difference.data()
            },
            {
                "intersection",
                (PyCFunction) intersection,
                METH_FASTCALL,
                ISet::docs::intersection.data()
            },
            {
                "symmetric_difference",
                (PyCFunction) symmetric_difference,
                METH_O,
                ISet::docs::symmetric_difference.data()
            },
            {
                "symmetric_difference_left",
                (PyCFunction) symmetric_difference_left,
                METH_O,
                ISet::docs::symmetric_difference_left.data()
            },
            {
                "__reversed__",
                (PyCFunction) Base::__reversed__,
                METH_NOARGS,
                Base::docs::__reversed__.data()
            },
            {NULL}
        };

        static PyTypeObject build_type() {
            PyTypeObject slots = {
                .ob_base = PyObject_HEAD_INIT(NULL)
                .tp_name = "LinkedDict_keys",
                .tp_basicsize = sizeof(PyKeysProxy),
                .tp_itemsize = 0,
                .tp_dealloc = (destructor) Base::__dealloc__,
                .tp_repr = (reprfunc) Base::__repr__,
                .tp_as_number = &number,
                .tp_as_sequence = &sequence,
                .tp_as_mapping = &mapping_methods,
                .tp_hash = (hashfunc) PyObject_HashNotImplemented,
                .tp_str = (reprfunc) __str__,
                .tp_flags = (
                    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_IMMUTABLETYPE |
                    Py_TPFLAGS_DISALLOW_INSTANTIATION | Py_TPFLAGS_SEQUENCE
                ),
                .tp_doc = PyDoc_STR(docs::PyKeysProxy.data()),
                .tp_richcompare = (richcmpfunc) Base::__richcompare__,
                .tp_iter = (getiterfunc) Base::__iter__,
                .tp_methods = methods,
                .tp_getset = properties,
                .tp_alloc = (allocfunc) PyType_GenericAlloc,
            };

            if (Py_IsInitialized() && PyType_Ready(&slots) < 0) {
                throw std::runtime_error("could not initialize DictProxy type");
            }
            return slots;
        }

    public:

        inline static PyTypeObject Type = build_type();

        static bool typecheck(PyObject* obj) {
            int result = PyObject_IsInstance(obj, (PyObject*) &Type);
            if (result == -1) {
                throw catch_python();
            }
            return static_cast<bool>(result);
        }

    };

    /* Python wrapper for LinkedDict.values(). */
    template <typename Proxy>
    class PyValuesProxy : public PyDictProxy<PyValuesProxy<Proxy>, Proxy> {
        friend PyDictInterface;
        using Base = PyDictProxy<PyValuesProxy, Proxy>;
        using IList = PyListInterface<Derived>;
        using ISet = PySetInterface<Derived>;

    public:

        static PyObject* __add__(PyValuesProxy* self, PyObject* other) {
            return Base::visit(self, [&other](auto& proxy) {
                return Base::unwrap_python(other, [&proxy](auto& other) {
                    return PyLinkedList::construct(proxy + other);
                });
            });
        }

        static PyObject* __mul__(PyValuesProxy* self, Py_ssize_t count) {
            return Base::visit(self, [&count](auto& proxy) {
                return PyLinkedList::construct(proxy * count);
            });
        }

        static PyObject* __str__(PyValuesProxy* self) {
            return Base::visit(self, [](auto& proxy) {
                std::ostringstream stream;
                stream << "[";
                auto it = proxy.begin();
                auto end = proxy.end();
                if (it != end) {
                    stream << repr(*it);
                    ++it;
                }
                while (it != end) {
                    stream << ", " << repr(*it);
                    ++it;
                }
                stream << "]";
                auto str = stream.str();
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            });
        }

    private:

        struct docs {

            static constexpr std::string_view PyValuesProxy {R"doc(
A read-only, listlike proxy for the values within a ``LinkedDict``.

Notes
-----
Instances of this class are returned by the :meth:`LinkedDict.values()`
accessor, which behaves in the same way as the built-in :meth:`dict.values()`.

These proxies support the following operations:

    #.  Iteration: ``len(vals)``, ``for val in vals: ...``, ``val in vals``
    #.  Indexing: ``vals[0]``, ``vals[1:3]``, ``vals.index(val)``,
        ``vals.count(val)``
    #.  List operators: ``vals + other``, ``vals * other``, ``vals < other``,
        ``vals <= other``, ``vals == other``, ``vals != other``,
        ``vals >= other``, ``vals > other``
)doc"
            };

        };

        inline static PyMappingMethods mapping_methods = [] {
            PyMappingMethods slots;
            slots.mp_length = (lenfunc) Base::__len__;
            slots.mp_subscript = (binaryfunc) Base::template __getitem__<PyLinkedList>;
            return slots;
        }();

        inline static PySequenceMethods sequence = [] {
            PySequenceMethods slots;
            slots.sq_length = (lenfunc) Base::__len__;
            slots.sq_concat = (binaryfunc) __add__;
            slots.sq_repeat = (ssizeargfunc) __mul__;
            slots.sq_item = (ssizeargfunc) Base::__getitem_scalar__;
            slots.sq_contains = (objobjproc) Base::__contains__;
            return slots;
        }();

        inline static PyGetSetDef properties[] = {
            {"mapping", (getter) Base::mapping, nullptr, Base::docs::mapping.data()},
            {NULL}
        };

        inline static PyMethodDef methods[] = {
            {"index", (PyCFunction) Base::index, METH_FASTCALL, IList::docs::index.data()},
            {"count", (PyCFunction) Base::count, METH_FASTCALL, IList::docs::count.data()},
            {
                "__reversed__",
                (PyCFunction) Base::__reversed__,
                METH_NOARGS,
                Base::docs::__reversed__.data()
            },
            {NULL}
        };

        static PyTypeObject build_type() {
            PyTypeObject slots = {
                .ob_base = PyObject_HEAD_INIT(NULL)
                .tp_name = "LinkedDict_values",
                .tp_basicsize = sizeof(PyValuesProxy),
                .tp_itemsize = 0,
                .tp_dealloc = (destructor) Base::__dealloc__,
                .tp_repr = (reprfunc) Base::__repr__,
                .tp_as_sequence = &sequence,
                .tp_as_mapping = &mapping_methods,
                .tp_hash = (hashfunc) PyObject_HashNotImplemented,
                .tp_str = (reprfunc) __str__,
                .tp_flags = (
                    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_IMMUTABLETYPE |
                    Py_TPFLAGS_DISALLOW_INSTANTIATION | Py_TPFLAGS_SEQUENCE
                ),
                .tp_doc = PyDoc_STR(docs::PyValuesProxy.data()),
                .tp_richcompare = (richcmpfunc) Base::__richcompare__,
                .tp_iter = (getiterfunc) Base::__iter__,
                .tp_methods = methods,
                .tp_getset = properties,
                .tp_alloc = (allocfunc) PyType_GenericAlloc,
            };

            if (Py_IsInitialized() && PyType_Ready(&slots) < 0) {
                throw std::runtime_error("could not initialize DictProxy type");
            }
            return slots;
        }

    public:

        inline static PyTypeObject Type = build_type();

        static bool typecheck(PyObject* obj) {
            int result = PyObject_IsInstance(obj, (PyObject*) &Type);
            if (result == -1) {
                throw catch_python();
            }
            return static_cast<bool>(result);
        }

    };

    /* Python wrapper for LinkedDict.items(). */
    template <typename Proxy>
    class PyItemsProxy : public PyDictProxy<PyItemsProxy<Proxy>, Proxy> {
        friend PyDictInterface;
        using Base = PyDictProxy<PyItemsProxy, Proxy>;
        using IList = PyListInterface<Derived>;
        using ISet = PySetInterface<Derived>;

    public:

        static PyObject* __add__(PyItemsProxy* self, PyObject* other) {
            return Base::visit(self, [&other](auto& proxy) {
                return PyDictInterface::unwrap_python(other, [&proxy](auto& other) {
                    return PyLinkedList::construct(proxy + other);
                });
            });
        }

        static PyObject* __mul__(PyItemsProxy* self, Py_ssize_t count) {
            return Base::visit(self, [&count](auto& proxy) {
                return PyLinkedList::construct(proxy * count);
            });
        }

        static PyObject* __getitem__(PyItemsProxy* self, PyObject* key) {
            using bertrand::util::parse_int;
            return Base::visit(self, [&key](auto& proxy) {
                if (PyIndex_Check(key)) {
                    python::Tuple<python::Ref::STEAL> item = proxy[parse_int(key)];
                    return item.unwrap();
                }

                if (PySlice_Check(key)) {
                    return PyLinkedList::construct(proxy.slice(key).get());
                }

                PyErr_Format(
                    PyExc_TypeError,
                    "indices must be integers or slices, not %s",
                    Py_TYPE(key)->tp_name
                );
                return static_cast<PyObject*>(nullptr);
            });
        }

        inline static PyObject* __iter__(PyItemsProxy* self) noexcept {
            return Base::visit(self, [&self](auto& proxy) {
                auto unwrap = [](python::Tuple<python::Ref::STEAL> item) {
                    return item.unwrap();  // relinquish ownership to Python
                };
                return iter(proxy, unwrap).cpython(reinterpret_cast<PyObject*>(self));
            });
        }

        inline static PyObject* __reversed__(
            PyItemsProxy* self,
            PyObject* = nullptr
        ) noexcept {
            return Base::visit(self, [&self](auto& proxy) {
                auto unwrap = [](python::Tuple<python::Ref::STEAL> item) {
                    return item.unwrap();  // relinquish ownership to Python
                };
                return iter(proxy, unwrap).crpython(reinterpret_cast<PyObject*>(self));
            });
        }

        static PyObject* __str__(PyItemsProxy* self) {
            return Base::visit(self, [](auto& proxy) {
                std::ostringstream stream;
                auto token = [&stream](const python::Tuple<python::Ref::STEAL>& item) {
                    stream << "(" << item[0].get().repr() << ", ";
                    stream << item[1].get().repr() << ")";
                };

                stream << "[";
                auto it = proxy.begin();
                auto end = proxy.end();
                if (it != end) {
                    token(*it);
                    ++it;
                }
                while (it != end) {
                    stream << ", ";
                    token(*it);
                    ++it;
                }
                stream << "]";
                auto str = stream.str();
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            });
        }

    private:

        /* Implement `PySequence_GetItem()` in CPython API. */
        static PyObject* __getitem_scalar__(PyItemsProxy* self, Py_ssize_t index) {
            return Base::visit(self, [&index](auto& proxy) {
                python::Tuple<python::Ref::STEAL> item = proxy[index];
                return item.unwrap();
            });
        }

        struct docs {

            static constexpr std::string_view PyItemsProxy {R"doc(
A read-only, listlike proxy for the key-value pairs contained within a
``LinkedDict``.

Notes
-----
Instances of this class are returned by the :meth:`LinkedDict.items()`
accessor, which behaves in the same way as the built-in :meth:`dict.items()`.

These proxies support the following operations:

    #.  Iteration: ``len(items)``, ``for key, value in items: ...``,
        ``item in items``
    #.  Indexing: ``items[0]``, ``items[1:3]``, ``items.index(key, value)``,
        ``items.index(item)``, ``items.count(key, value)``,
        ``items.count(item)``
    #.  List operators: ``items + other``, ``items * other``,
        ``items < other``, ``items <= other``, ``items == other``,
        ``items != other``, ``items >= other``, ``items > other``
)doc"
            };

        };

        inline static PyMappingMethods mapping_methods = [] {
            PyMappingMethods slots;
            slots.mp_length = (lenfunc) Base::__len__;
            slots.mp_subscript = (binaryfunc) __getitem__;
            return slots;
        }();

        inline static PySequenceMethods sequence = [] {
            PySequenceMethods slots;
            slots.sq_length = (lenfunc) Base::__len__;
            slots.sq_concat = (binaryfunc) __add__;
            slots.sq_repeat = (ssizeargfunc) __mul__;
            slots.sq_item = (ssizeargfunc) __getitem_scalar__;
            slots.sq_contains = (objobjproc) Base::__contains__;
            return slots;
        }();

        inline static PyGetSetDef properties[] = {
            {"mapping", (getter) Base::mapping, nullptr, Base::docs::mapping.data()},
            {NULL}
        };

        inline static PyMethodDef methods[] = {
            {"index", (PyCFunction) Base::index, METH_FASTCALL, IList::docs::index.data()},
            {"count", (PyCFunction) Base::count, METH_FASTCALL, IList::docs::count.data()},
            {
                "__reversed__",
                (PyCFunction) __reversed__,
                METH_NOARGS,
                Base::docs::__reversed__.data()
            },
            {NULL}
        };

        static PyTypeObject build_type() {
            PyTypeObject slots = {
                .ob_base = PyObject_HEAD_INIT(NULL)
                .tp_name = "LinkedDict_items",
                .tp_basicsize = sizeof(PyItemsProxy),
                .tp_itemsize = 0,
                .tp_dealloc = (destructor) Base::__dealloc__,
                .tp_repr = (reprfunc) Base::__repr__,
                .tp_as_sequence = &sequence,
                .tp_as_mapping = &mapping_methods,
                .tp_hash = (hashfunc) PyObject_HashNotImplemented,
                .tp_str = (reprfunc) __str__,
                .tp_flags = (
                    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_IMMUTABLETYPE |
                    Py_TPFLAGS_DISALLOW_INSTANTIATION | Py_TPFLAGS_SEQUENCE
                ),
                .tp_doc = PyDoc_STR(docs::PyItemsProxy.data()),
                .tp_richcompare = (richcmpfunc) Base::__richcompare__,
                .tp_iter = (getiterfunc) __iter__,
                .tp_methods = methods,
                .tp_getset = properties,
                .tp_alloc = (allocfunc) PyType_GenericAlloc,
            };

            if (Py_IsInitialized() && PyType_Ready(&slots) < 0) {
                throw std::runtime_error("could not initialize DictProxy type");
            }
            return slots;
        }

    public:

        inline static PyTypeObject Type = build_type();

        static bool typecheck(PyObject* obj) {
            int result = PyObject_IsInstance(obj, (PyObject*) &Type);
            if (result == -1) {
                throw catch_python();
            }
            return static_cast<bool>(result);
        }

    };

};


/* A Python type that exposes a discriminated union of C++ LinkedDicts to the Python
interpreter. */
class PyLinkedDict :
    public PyLinkedBase<PyLinkedDict>,
    public PyListInterface<PyLinkedDict>,
    public PySetInterface<PyLinkedDict>,
    public PyDictInterface<PyLinkedDict>
{
    using Base = PyLinkedBase<PyLinkedDict>;
    using IList = PyListInterface<PyLinkedDict>;
    using ISet = PySetInterface<PyLinkedDict>;
    using IDict = PyDictInterface<PyLinkedDict>;

    /* A std::variant representing all of the LinkedDict implementations that are
    constructable from Python. */
    template <unsigned int Flags>
    using DictConfig = linked::LinkedDict<PyObject*, PyObject*, Flags, BasicLock>;
    using Variant = std::variant<
        DictConfig<Config::DEFAULT>,
        DictConfig<Config::STRICTLY_TYPED>,
        DictConfig<Config::FIXED_SIZE>,
        DictConfig<Config::FIXED_SIZE | Config::STRICTLY_TYPED>,
        DictConfig<Config::SINGLY_LINKED>,
        DictConfig<Config::SINGLY_LINKED | Config::STRICTLY_TYPED>,
        DictConfig<Config::SINGLY_LINKED | Config::FIXED_SIZE>,
        DictConfig<Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::STRICTLY_TYPED>
    >;
    template <size_t I>
    using Alt = typename std::variant_alternative_t<I, Variant>;

    friend Base;
    friend IList;
    friend ISet;
    friend IDict;
    Variant variant;

    /* Construct a PyLinkedDict around an existing C++ LinkedDict. */
    template <typename Dict>
    inline void from_cpp(Dict&& dict) {
        new (&variant) Variant(std::forward<Dict>(dict));
    }

    /* Parse the configuration code and initialize the variant with the forwarded
    arguments. */
    template <typename... Args>
    static void build_variant(unsigned int code, PyLinkedDict* self, Args&&... args) {
        switch (code) {
            case (Config::DEFAULT):
                self->from_cpp(Alt<0>(std::forward<Args>(args)...));
                break;
            case (Config::STRICTLY_TYPED):
                self->from_cpp(Alt<1>(std::forward<Args>(args)...));
                break;
            case (Config::FIXED_SIZE):
                self->from_cpp(Alt<2>(std::forward<Args>(args)...));
                break;
            case (Config::FIXED_SIZE | Config::STRICTLY_TYPED):
                self->from_cpp(Alt<3>(std::forward<Args>(args)...));
                break;
            case (Config::SINGLY_LINKED):
                self->from_cpp(Alt<4>(std::forward<Args>(args)...));
                break;
            case (Config::SINGLY_LINKED | Config::STRICTLY_TYPED):
                self->from_cpp(Alt<5>(std::forward<Args>(args)...));
                break;
            case (Config::SINGLY_LINKED | Config::FIXED_SIZE):
                self->from_cpp(Alt<6>(std::forward<Args>(args)...));
                break;
            case (Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::STRICTLY_TYPED):
                self->from_cpp(Alt<7>(std::forward<Args>(args)...));
                break;
            default:  // should never happen
                throw ValueError("invalid argument configuration");
        }
    }

    /* Parse the configuration code and initialize the variant with the forwarded
    arguments from a Python-level fromkeys() method. */
    template <typename... Args>
    static void build_fromkeys(unsigned int code, PyLinkedDict* self, Args&&... args) {
        switch (code) {
            case (Config::DEFAULT):
                self->from_cpp(Alt<0>::fromkeys(std::forward<Args>(args)...));
                break;
            case (Config::STRICTLY_TYPED):
                self->from_cpp(Alt<1>::fromkeys(std::forward<Args>(args)...));
                break;
            case (Config::FIXED_SIZE):
                self->from_cpp(Alt<2>::fromkeys(std::forward<Args>(args)...));
                break;
            case (Config::FIXED_SIZE | Config::STRICTLY_TYPED):
                self->from_cpp(Alt<3>::fromkeys(std::forward<Args>(args)...));
                break;
            case (Config::SINGLY_LINKED):
                self->from_cpp(Alt<4>::fromkeys(std::forward<Args>(args)...));
                break;
            case (Config::SINGLY_LINKED | Config::STRICTLY_TYPED):
                self->from_cpp(Alt<5>::fromkeys(std::forward<Args>(args)...));
                break;
            case (Config::SINGLY_LINKED | Config::FIXED_SIZE):
                self->from_cpp(Alt<6>::fromkeys(std::forward<Args>(args)...));
                break;
            case (Config::SINGLY_LINKED | Config::FIXED_SIZE | Config::STRICTLY_TYPED):
                self->from_cpp(Alt<7>::fromkeys(std::forward<Args>(args)...));
                break;
            default:  // should never happen
                throw ValueError("invalid argument configuration");
        }
    }

    /* Construct a PyLinkedDict from a Python-level __init__() method, or one of its
    specialized equivalents using __class_getitem__().  Optional constructor arguments
    are used to determine the appropriate template configuration. */
    static void initialize(
        PyLinkedDict* self,
        PyObject* iterable,
        std::optional<size_t> max_size,
        PyObject* spec,
        bool reverse,
        bool singly_linked,
        bool strictly_typed
    ) {
        unsigned int code = (
            Config::SINGLY_LINKED * singly_linked |
            Config::FIXED_SIZE * max_size.has_value() |
            Config::STRICTLY_TYPED * strictly_typed
        );
        if (iterable == nullptr) {
            build_variant(code, self, max_size, spec);
        } else {
            if (typecheck(iterable)) {
                std::visit(
                    [&](auto& dict) {
                        build_variant(
                            code,
                            self,
                            dict,
                            max_size,
                            spec,
                            reverse
                        );
                    },
                    reinterpret_cast<PyLinkedDict*>(iterable)->variant
                );
            } else {
                build_variant(code, self, iterable, max_size, spec, reverse);
            }
        }
    }

public:
    static constexpr std::string_view NAME{"PyLinkedDict"};  // used in logging

    static int __init__(PyLinkedDict* self, PyObject* args, PyObject* kwargs) {
        static constexpr std::string_view meth_name{"__init__"};
        using bertrand::util::parse_int;
        using bertrand::util::none_to_null;
        using bertrand::util::is_truthy;

        try {
            PyArgs<CallProtocol::KWARGS> pyargs(meth_name, args, kwargs);
            PyObject* iterable = pyargs.parse(
                "iterable", none_to_null, (PyObject*) nullptr
            );
            std::optional<size_t> max_size = pyargs.parse(
                "max_size",
                [](PyObject* obj) -> std::optional<size_t> {
                    if (obj == Py_None) {
                        return std::nullopt;
                    }
                    long long result = parse_int(obj);
                    if (result < 0) {
                        throw ValueError("max_size cannot be negative");
                    }
                    return std::make_optional(static_cast<size_t>(result));
                },
                std::optional<size_t>()
            );
            PyObject* spec = pyargs.parse("spec", none_to_null, (PyObject*) nullptr);
            bool reverse = pyargs.parse("reverse", is_truthy, false);
            bool singly_linked = pyargs.parse("singly_linked", is_truthy, false);
            pyargs.finalize();

            initialize(
                self, iterable, max_size, spec, reverse, singly_linked, false
            );

            return 0;

        } catch (...) {
            throw_python();
            return -1;
        }
    }

    static PyObject* fromkeys(PyObject* type, PyObject* args, PyObject* kwargs) {
        PyLinkedDict* self = reinterpret_cast<PyLinkedDict*>(
            Type.tp_new(&Type, nullptr, nullptr)
        );
        if (self == nullptr) {
            return nullptr;
        }

        static constexpr std::string_view meth_name{"fromkeys"};
        using bertrand::util::parse_int;
        using bertrand::util::none_to_null;
        using bertrand::util::is_truthy;

        try {
            PyArgs<CallProtocol::KWARGS> pyargs(meth_name, args, kwargs);
            PyObject* keys = pyargs.parse("keys");
            PyObject* value = pyargs.parse("value", identity(), Py_None);
            std::optional<size_t> max_size = pyargs.parse(
                "max_size",
                [](PyObject* obj) -> std::optional<size_t> {
                    if (obj == Py_None) {
                        return std::nullopt;
                    }
                    long long result = parse_int(obj);
                    if (result < 0) {
                        throw ValueError("max_size cannot be negative");
                    }
                    return std::make_optional(static_cast<size_t>(result));
                },
                std::optional<size_t>()
            );
            PyObject* spec = pyargs.parse("spec", none_to_null, (PyObject*) nullptr);
            bool singly_linked = pyargs.parse("singly_linked", is_truthy, false);
            pyargs.finalize();

            unsigned int code = (
                Config::SINGLY_LINKED * singly_linked |
                Config::FIXED_SIZE * max_size.has_value()
            );

            build_fromkeys(code, self, keys, value, max_size, spec);
            return reinterpret_cast<PyObject*>(self);
    
        } catch (...) {
            Py_DECREF(self);
            throw_python();
            return nullptr;
        }
    }

    static PyObject* __str__(PyLinkedDict* self) {
        return Base::visit(self, [](auto& dict) {
            std::ostringstream stream;
            stream << "{";
            auto it = dict.begin();
            auto end = dict.end();
            if (it != end) {
                stream << repr(*it) << ": ";
                stream << repr(it.curr()->mapped());
                ++it;
            }
            while (it != end) {
                stream << ", " << repr(*it) << ": ";
                stream << repr(it.curr()->mapped());
                ++it;
            }
            stream << "}";
            auto str = stream.str();
            return PyUnicode_FromStringAndSize(str.c_str(), str.size());
        });
    }

    static PyObject* __class_getitem__(PyObject* type, PyObject* spec) {
        PyObject* heap_type = PyType_FromSpecWithBases(&Specialized::py_spec, type);
        if (heap_type == nullptr) {
            return nullptr;
        }
        if (PyObject_SetAttrString(heap_type, "_specialization", spec) < 0) {
            Py_DECREF(heap_type);
            return nullptr;
        }
        return heap_type;
    }

private:

    struct docs {

        static constexpr std::string_view LinkedDict {R"doc(
A modular, ordered dictionary based on a linked list available in both Python
and C++.

This class is a drop-in replacement for a built-in :class:`dict`, supporting
all the same operations, plus some from the :class:`set` and :class:`list`
interfaces, as well as extras leveraging the ordered nature of the dictionary.
It is also available as a C++ type under the same name, with identical
semantics.

Parameters
----------
items : Iterable[Any], optional
    The key-value pairs to initialize the dictionary with.  This can be any
    mapping type or iterable of pairs of the form ``(key, value)``.  If not
    specified, the dictionary will be empty.
max_size : int, optional
    The maximum number of keys that the dictionary can hold.  If not specified,
    the dictionary will be unbounded.
spec : Any, optional
    A specific type to enforce for elements of the dictionary, allowing the
    creation of type-safe containers.  This can be in any format recognized by
    :func:`isinstance() <python:isinstance>`, and can be provided as a
    :class:`slice` to specialize the key and value types separately.  By
    default, only the keys will be specialized.  The default is ``None``, which
    disables type checking for the dictionary.  See the :meth:`specialize()`
    method for more details.
reverse : bool, default False
    If True, reverse the order of ``items`` during dictionary construction.
    This is more efficient than calling :meth:`reverse()` after construction.
singly_linked : bool, default False
    If True, use a singly-linked dictionary instead of a doubly-linked one.
    This trades some performance in certain operations for increased memory
    efficiency.  Regardless of this setting, the dictionary will still support
    all the same operations.

Notes
-----
These data structures are highly optimized, and offer performance that is
generally on par with the built-in :class:`dict` type.  They have slightly more
overhead due to handling the links between each node, but users should not
notice a significant difference on average.

The data structure itself is implemented entirely in C++, and can be used
equivalently at the C++ level.  In fact, the Python wrapper is just a
discriminated union of C++ templates, and can be thought of as directly emitting
equivalent C++ code at runtime.  As such, each variation of this data structure
is available as a C++ type under the same name, with identical semantics and
only superficial syntax differences related to both languages.  Here's an
example:

.. code-block:: cpp

    #include <bertrand/structs/linked/list.h>

    int main() {
        using Item = std::pair<std::string, int>
        std::vector<Item> items = {
            {"a", 1}, {"b", 2}, {"c", 3}, {"d", 4}, {"e", 5}
        };
        bertrand::LinkedDict<std::string, int> dict(items);

        dict["f"] = 6;
        dict.update(std::vector<Item>{{"g", 7}, {"h", 8}, {"i", 9}});
        int x = dict.pop("i");
        dict.rotate(4);
        dict["i"] = x;
        for (Item i : dict.items()) {
            // ...
        }

        std::cout << dict;
        // LinkedDict({"f": 6, "g": 7, "h": 8, "a": 1, "b": 2, "c": 3, "d": 4, "i": 9})
        return 0;
    }

This makes it significantly easier to port code that relies on this data
structure between the two languages.  In fact, doing so provides significant
benefits, allowing users to take advantage of static C++ types and completely
bypass the Python interpreter, increasing performance by orders of magnitude
in some cases.
)doc"
        };

        static constexpr std::string_view fromkeys {R"doc(
Create a new ``LinkedDict`` with keys from ``iterable`` and values set to
``value``.

This is a class method.

Parameters
----------
iterable : Iterable[Hashable]
    An iterable of keys to use for the new ``LinkedDict``.
value : Any, default None
    The value to use for all keys in ``iterable``.
    
Returns
-------
LinkedDict
    A new ``LinkedDict`` with keys from ``iterable`` and values set to
    ``value``.

Notes
-----
Note that all keys will refer to just a singly instance of ``value``.  This can
lead to unexpected behavior if ``value`` is a mutable object, such as a list.
To get distinct values, use a generator expression in the ``LinkedDict``
constructor itself.
)doc"
        };

    };

    ////////////////////////////////
    ////    PYTHON INTERNALS    ////
    ////////////////////////////////

    #define BASE_PROPERTY(NAME) \
        { #NAME, (getter) Base::NAME, NULL, PyDoc_STR(Base::docs::NAME.data()) } \

    #define BASE_METHOD(NAME, ARG_PROTOCOL) \
        { #NAME, (PyCFunction) Base::NAME, ARG_PROTOCOL, PyDoc_STR(Base::docs::NAME.data()) } \

    #define LIST_METHOD(NAME, ARG_PROTOCOL) \
        { #NAME, (PyCFunction) IList::NAME, ARG_PROTOCOL, PyDoc_STR(IList::docs::NAME.data()) } \

    #define SET_METHOD(NAME, ARG_PROTOCOL) \
        { #NAME, (PyCFunction) ISet::NAME, ARG_PROTOCOL, PyDoc_STR(ISet::docs::NAME.data()) } \

    #define DICT_METHOD(NAME, ARG_PROTOCOL) \
        { #NAME, (PyCFunction) IDict::NAME, ARG_PROTOCOL, PyDoc_STR(IDict::docs::NAME.data()) } \

    inline static PyGetSetDef properties[] = {
        BASE_PROPERTY(SINGLY_LINKED),
        BASE_PROPERTY(DOUBLY_LINKED),
        BASE_PROPERTY(FIXED_SIZE),
        BASE_PROPERTY(DYNAMIC),
        BASE_PROPERTY(STRICTLY_TYPED),
        BASE_PROPERTY(LOOSELY_TYPED),
        BASE_PROPERTY(lock),
        BASE_PROPERTY(capacity),
        BASE_PROPERTY(max_size),
        BASE_PROPERTY(frozen),
        BASE_PROPERTY(specialization),
        BASE_PROPERTY(nbytes),
        {NULL}
    };

    inline static PyMethodDef methods[] = {
        BASE_METHOD(reserve, METH_FASTCALL),
        BASE_METHOD(defragment, METH_NOARGS),
        BASE_METHOD(specialize, METH_O),
        BASE_METHOD(__reversed__, METH_NOARGS),
        LIST_METHOD(index, METH_FASTCALL),
        LIST_METHOD(count, METH_FASTCALL),
        LIST_METHOD(clear, METH_NOARGS),
        LIST_METHOD(copy, METH_NOARGS),
        LIST_METHOD(sort, METH_FASTCALL | METH_KEYWORDS),
        LIST_METHOD(reverse, METH_NOARGS),
        LIST_METHOD(rotate, METH_FASTCALL),
        SET_METHOD(remove, METH_O),
        SET_METHOD(discard, METH_O),
        SET_METHOD(lru_contains, METH_O),
        {
            "union",  // renamed
            (PyCFunction) union_,
            METH_FASTCALL,
            PyDoc_STR(ISet::docs::union_.data())
        },
        SET_METHOD(union_left, METH_FASTCALL),
        SET_METHOD(update, METH_FASTCALL),
        SET_METHOD(update_left, METH_FASTCALL),
        SET_METHOD(lru_update, METH_FASTCALL),
        SET_METHOD(difference, METH_FASTCALL),
        SET_METHOD(difference_update, METH_FASTCALL),
        SET_METHOD(intersection, METH_FASTCALL),
        SET_METHOD(intersection_update, METH_FASTCALL),
        SET_METHOD(symmetric_difference, METH_O),
        SET_METHOD(symmetric_difference_left, METH_O),
        SET_METHOD(symmetric_difference_update, METH_O),
        SET_METHOD(symmetric_difference_update_left, METH_O),
        SET_METHOD(distance, METH_FASTCALL),
        SET_METHOD(swap, METH_FASTCALL),
        SET_METHOD(move, METH_FASTCALL),
        SET_METHOD(move_to_index, METH_FASTCALL),
        {
            "fromkeys",
            (PyCFunction) fromkeys,
            METH_VARARGS | METH_KEYWORDS | METH_CLASS,
            PyDoc_STR(docs::fromkeys.data())
        },
        {
            "__class_getitem__",
            (PyCFunction) __class_getitem__,
            METH_CLASS | METH_O,
            PyDoc_STR(Base::docs::__class_getitem__.data())
        },
        DICT_METHOD(add, METH_FASTCALL),
        DICT_METHOD(add_left, METH_FASTCALL),
        DICT_METHOD(lru_add, METH_FASTCALL),
        DICT_METHOD(insert, METH_FASTCALL),
        DICT_METHOD(pop, METH_FASTCALL),
        DICT_METHOD(popitem, METH_FASTCALL),
        DICT_METHOD(get, METH_FASTCALL),
        DICT_METHOD(lru_get, METH_FASTCALL),
        DICT_METHOD(setdefault, METH_FASTCALL),
        DICT_METHOD(setdefault_left, METH_FASTCALL),
        DICT_METHOD(lru_setdefault, METH_FASTCALL),
        DICT_METHOD(keys, METH_NOARGS),
        DICT_METHOD(values, METH_NOARGS),
        DICT_METHOD(items, METH_NOARGS),
        {NULL}
    };

    #undef BASE_PROPERTY
    #undef BASE_METHOD
    #undef LIST_METHOD
    #undef SET_METHOD
    #undef DICT_METHOD

    inline static PyMappingMethods mapping = [] {
        PyMappingMethods slots;
        slots.mp_length = (lenfunc) Base::__len__;
        slots.mp_subscript = (binaryfunc) IDict::__getitem__;
        slots.mp_ass_subscript = (objobjargproc) IDict::__setitem__;
        return slots;
    }();

    inline static PySequenceMethods sequence = [] {
        PySequenceMethods slots;
        slots.sq_length = (lenfunc) Base::__len__;
        slots.sq_item = (ssizeargfunc) IDict::__getitem_scalar__;
        slots.sq_ass_item = (ssizeobjargproc) IList::__setitem_scalar__;
        slots.sq_contains = (objobjproc) IList::__contains__;
        return slots;
    }();

    inline static PyNumberMethods number = [] {
        PyNumberMethods slots;
        slots.nb_or = (binaryfunc) ISet::__or__;
        slots.nb_inplace_or = (binaryfunc) ISet::__ior__;
        slots.nb_subtract = (binaryfunc) ISet::__sub__;
        slots.nb_inplace_subtract = (binaryfunc) ISet::__isub__;
        slots.nb_and = (binaryfunc) ISet::__and__;
        slots.nb_inplace_and = (binaryfunc) ISet::__iand__;
        slots.nb_xor = (binaryfunc) ISet::__xor__;
        slots.nb_inplace_xor = (binaryfunc) ISet::__ixor__;
        return slots;
    }();

    static PyTypeObject build_type() {
        return {
            .ob_base = PyObject_HEAD_INIT(NULL)
            .tp_name = "bertrand.LinkedDict",
            .tp_basicsize = sizeof(PyLinkedDict),
            .tp_itemsize = 0,
            .tp_dealloc = (destructor) Base::__dealloc__,
            .tp_repr = (reprfunc) Base::__repr__,
            .tp_as_number = &number,
            .tp_as_sequence = &sequence,
            .tp_as_mapping = &mapping,
            .tp_hash = (hashfunc) PyObject_HashNotImplemented,
            .tp_str = (reprfunc) __str__,
            .tp_flags = (
                Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC |
                Py_TPFLAGS_IMMUTABLETYPE | Py_TPFLAGS_SEQUENCE
                // add Py_TPFLAGS_MANAGED_WEAKREF for Python 3.12+
            ),
            .tp_doc = PyDoc_STR(docs::LinkedDict.data()),
            .tp_traverse = (traverseproc) Base::__traverse__,
            .tp_clear = (inquiry) Base::__clear__,
            .tp_richcompare = (richcmpfunc) IDict::__richcompare__,
            .tp_iter = (getiterfunc) Base::__iter__,
            .tp_methods = methods,
            .tp_getset = properties,
            .tp_init = (initproc) __init__,
            .tp_alloc = (allocfunc) PyType_GenericAlloc,
            .tp_new = (newfunc) PyType_GenericNew,
            .tp_free = (freefunc) PyObject_GC_Del,
        };
    };

public:

    inline static PyTypeObject Type = build_type();

    /* Allocate and construct a fully-formed PyLinkedDict from its C++ equivalent. */
    template <typename Dict>
    static PyObject* construct(Dict&& dict) {
        PyLinkedDict* result = reinterpret_cast<PyLinkedDict*>(
            Type.tp_new(&Type, nullptr, nullptr)
        );
        if (result == nullptr) {
            return nullptr;
        }

        try {
            result->from_cpp(std::forward<Dict>(dict));
            return reinterpret_cast<PyObject*>(result);
        } catch (...) {
            Py_DECREF(result);
            throw;
        }
    }

    /* Check whether another PyObject* is of this type. */
    static bool typecheck(PyObject* obj) {
        int result = PyObject_IsInstance(obj, (PyObject*) &Type);
        if (result == -1) {
            throw catch_python();
        }
        return static_cast<bool>(result);
    }

private:

    /* Dynamic heap type generated by `LinkedDict.__class_getitem__()` in Python. */
    class Specialized : public Base::Specialized {

        static PyObject* fromkeys(PyObject* type, PyObject* args, PyObject* kwargs) {
            PyLinkedDict* self = reinterpret_cast<PyLinkedDict*>(
                Type.tp_new(&Type, nullptr, nullptr)
            );
            if (self == nullptr) {
                return nullptr;
            }
            PyObject* spec = PyObject_GetAttrString(type, "_specialization");
            if (spec == nullptr) {
                Py_DECREF(self);
                return nullptr;
            }

            static constexpr std::string_view meth_name{"fromkeys"};
            using bertrand::util::parse_int;
            using bertrand::util::none_to_null;
            using bertrand::util::is_truthy;
            try {
                PyArgs<CallProtocol::KWARGS> pyargs(meth_name, args, kwargs);
                PyObject* keys = pyargs.parse("keys");
                PyObject* value = pyargs.parse("value", identity(), Py_None);
                std::optional<size_t> max_size = pyargs.parse(
                    "max_size",
                    [](PyObject* obj) -> std::optional<size_t> {
                        if (obj == Py_None) {
                            return std::nullopt;
                        }
                        long long result = parse_int(obj);
                        if (result < 0) {
                            throw ValueError("max_size cannot be negative");
                        }
                        return std::make_optional(static_cast<size_t>(result));
                    },
                    std::optional<size_t>()
                );
                bool singly_linked = pyargs.parse("singly_linked", is_truthy, false);
                pyargs.finalize();

                unsigned int code = (
                    Config::SINGLY_LINKED * singly_linked |
                    Config::FIXED_SIZE * max_size.has_value() |
                    Config::STRICTLY_TYPED
                );

                build_fromkeys(code, self, keys, value, max_size, spec);
                return reinterpret_cast<PyObject*>(self);
        
            } catch (...) {
                Py_DECREF(self);
                Py_DECREF(spec);
                throw_python();
                return nullptr;
            }
        }

    private:
        using BaseSpec = Base::Specialized;

        /* Overridden methods for permanently-specialized types. */
        inline static PyMethodDef specialized_methods[] = {
            {"__init__", (PyCFunction) BaseSpec::__init__, METH_VARARGS | METH_KEYWORDS, nullptr},
            {"fromkeys", (PyCFunction) fromkeys, METH_VARARGS | METH_KEYWORDS | METH_CLASS, nullptr},
            {NULL}
        };

        /* Overridden slots for permanently-specialized types. */
        inline static PyType_Slot specialized_slots[] = {
            {Py_tp_init, (void*) __init__},
            {Py_tp_methods, specialized_methods},
            {0}
        };

    public:

        /* Overridden type definition for permanently-specialized types. */
        inline static PyType_Spec py_spec = {
            .name = PyLinkedDict::Type.tp_name,
            .basicsize = 0,  // inherited from base class
            .itemsize = 0,
            .flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,
            .slots = specialized_slots
        };

    };

};


}  // namespace linked


using linked::LinkedDict;
using linked::PyLinkedDict;


}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_DICT_H
