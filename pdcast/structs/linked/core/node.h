#ifndef BERTRAND_STRUCTS_LINKED_CORE_NODE_H
#define BERTRAND_STRUCTS_LINKED_CORE_NODE_H

#include <sstream>  // std::ostringstream
#include <type_traits>  // std::enable_if_t<>, etc.
#include <Python.h>  // CPython API
#include "../../util/base.h"  // is_pyobject<>
#include "../../util/except.h"  // catch_python(), TypeError()
#include "../../util/func.h"  // FuncTraits
#include "../../util/ops.h"  // hash(), repr()


namespace bertrand {
namespace structs {
namespace linked {


////////////////////
////    BASE    ////
////////////////////


/* Empty tag class marking a node for a linked data structure.  This class is inherited
by all nodes, and can be used for easy SFINAE checks via std::is_base_of, without
requiring any foreknowledge of template parameters. */
class NodeTag {};


/* Base class containing common functionality across all nodes. */
template <typename ValueType>
class BaseNode : public NodeTag {
public:
    using Value = ValueType;

    /* Get the value held by the node. */
    inline Value value() const noexcept {
        return _value;
    }

    /* Apply an explicit type check to the value if it is a Python object. */
    template <bool cond = is_pyobject<Value>>
    inline std::enable_if_t<cond, bool> typecheck(PyObject* specialization) const {
        if (specialization == nullptr) {
            return true;
        }
        int comp = PyObject_IsInstance(_value, specialization);
        if (comp == -1) {
            throw catch_python();
        }
        return comp;
    }

protected:
    Value _value;

    /* Initialize a node with a given value. */
    inline BaseNode(const Value& value) noexcept : _value(value) {
        if constexpr (is_pyobject<Value>) {
            Py_XINCREF(value);
        }
    }

    /* Copy constructor. */
    inline BaseNode(const BaseNode& other) noexcept : _value(other._value) {
        if constexpr (is_pyobject<Value>) {
            Py_XINCREF(_value);
        }
    }

    /* Move constructor. */
    inline BaseNode(BaseNode&& other) noexcept : _value(std::move(other._value)) {
        if constexpr (std::is_pointer_v<Value>) {
            other._value = nullptr;
        }
    }

    /* Copy assignment operator. */
    inline BaseNode& operator=(const BaseNode& other) noexcept {
        if (this == &other) {
            return *this;
        }

        // clear current node
        if constexpr (is_pyobject<Value>) {
            Py_XDECREF(_value);
        }

        // copy other node
        _value = other._value;
        if constexpr (is_pyobject<Value>) {
            Py_XINCREF(_value);
        }
        return *this;
    }

    /* Move assignment operator. */
    inline BaseNode& operator=(BaseNode&& other) noexcept {
        if (this == &other) {
            return *this;
        }

        // clear current node
        if constexpr (is_pyobject<Value>) {
            Py_XDECREF(_value);
        }

        // move other node
        _value = std::move(other._value);
        if constexpr (std::is_pointer_v<Value>) {
            other._value = nullptr;
        }
        return *this;
    }

    /* Destroy a node and release its resources. */
    inline ~BaseNode() noexcept {
        if constexpr (is_pyobject<Value>) {
            Py_XDECREF(_value);
        }
        if constexpr (std::is_pointer_v<Value>) {
            _value = nullptr;
        }
    }

};


//////////////////////////
////    ROOT NODES    ////
//////////////////////////


// NOTE: at some point in the future, we could try to implement an XOR linked list
// using an XORNode with identical semantics to DoubleNode.  This would only use a
// single pointer to store both the next and previous nodes, which it would XOR in
// order to traverse the list.  This would require some care when accessing neighboring
// nodes, since we would have to keep track of the previous node in order to compute
// the next node's address and vice versa.  This would also complicate the
// insertion/removal of nodes, since we would have to XOR the next/previous nodes in
// order to update their pointers.  But, it would be a fun exercise in pointer
// arithmetic with some interesting tradeoffs.  It would make for a neat benchmark too.


/* A singly-linked list node around an arbitrary value. */
template <typename ValueType>
class SingleNode : public BaseNode<ValueType> {
    using Base = BaseNode<ValueType>;
    SingleNode* _next;

public:
    using Value = ValueType;
    static constexpr bool doubly_linked = false;

    /* Initialize a singly-linked node with a given value. */
    SingleNode(const Value& value) noexcept : Base(value), _next(nullptr) {}

    /* Copy constructor. */
    SingleNode(const SingleNode& other) noexcept : Base(other), _next(nullptr) {}

    /* Move constructor. */
    SingleNode(SingleNode&& other) noexcept :
        Base(std::move(other)), _next(other._next)
    {
        other._next = nullptr;
    }

    /* Copy assignment operator. */
    SingleNode& operator=(const SingleNode& other) noexcept {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        _next = nullptr;
        return *this;
    }

    /* Move assignment operator. */
    SingleNode& operator=(SingleNode&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        _next = other._next;
        other._next = nullptr;
        return *this;
    }

    /* Destroy a singly-linked node and release its resources. */
    ~SingleNode() noexcept {
        _next = nullptr;
    }

    /* Get the next node in the list. */
    SingleNode* next() const noexcept {
        return _next;
    }

    /* Set the next node in the list. */
    inline void next(SingleNode* next) noexcept {
        _next = next;
    }

    /* Link the node to its neighbors to form a singly-linked list. */
    inline static void link(
        SingleNode* prev,
        SingleNode* curr,
        SingleNode* next
    ) noexcept {
        if (prev != nullptr) {
            prev->next(curr);
        }
        curr->next(next);
    }

    /* Unlink the node from its neighbors. */
    inline static void unlink(
        SingleNode* prev,
        SingleNode* curr,
        SingleNode* next
    ) noexcept {
        if (prev != nullptr) {
            prev->next(next);
        }
    }

    /* Break a linked list at a specific junction. */
    inline static void split(SingleNode* prev, SingleNode* curr) noexcept {
        if (prev != nullptr) {
            prev->next(nullptr);
        }
    }

    /* Join the list at a specific junction. */
    inline static void join(SingleNode* prev, SingleNode* curr) noexcept {
        if (prev != nullptr) {
            prev->next(curr);
        }
    }

};


/* A doubly-linked list node around an arbitrary value. */
template <typename ValueType>
class DoubleNode : public SingleNode<ValueType> {
    using Base = SingleNode<ValueType>;
    DoubleNode* _prev;

public:
    using Value = ValueType;
    static constexpr bool doubly_linked = true;

    /* Initialize a doubly-linked node with a given value. */
    DoubleNode(const Value& value) noexcept : Base(value), _prev(nullptr) {}

    /* Copy constructor. */
    DoubleNode(const DoubleNode& other) noexcept : Base(other), _prev(nullptr) {}

    /* Move constructor. */
    DoubleNode(DoubleNode&& other) noexcept :
        Base(std::move(other)), _prev(other._prev)
    {
        other._prev = nullptr;
    }

    /* Copy assignment operator. */
    DoubleNode& operator=(const DoubleNode& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        _prev = nullptr;
        return *this;
    }

    /* Move assignment operator. */
    DoubleNode& operator=(DoubleNode&& other) {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        _prev = other._prev;
        other._prev = nullptr;
        return *this;
    }

    /* Destroy a doubly-linked node and release its resources. */
    ~DoubleNode() noexcept {
        _prev = nullptr;
    }

    /* Get the next node in the list. */
    inline DoubleNode* next() const noexcept {
        return static_cast<DoubleNode*>(Base::next());
    }

    /* Set the next node in the list. */
    inline void next(DoubleNode* next) noexcept {
        Base::next(next);
    }

    /* Get the previous node in the list. */
    inline DoubleNode* prev() const noexcept {
        return _prev;
    }

    /* Set the previous node in the list. */
    inline void prev(DoubleNode* prev) noexcept {
        _prev = prev;
    }

    /* Link the node to its neighbors to form a doubly-linked list. */
    inline static void link(
        DoubleNode* prev,
        DoubleNode* curr,
        DoubleNode* next
    ) noexcept {
        if (prev != nullptr) {
            prev->next(curr);
        }
        curr->prev(prev);
        curr->next(next);
        if (next != nullptr) {
            next->prev(curr);
        }
    }

    /* Unlink the node from its neighbors. */
    inline static void unlink(
        DoubleNode* prev,
        DoubleNode* curr,
        DoubleNode* next
    ) noexcept {
        if (prev != nullptr) {
            prev->next(next);
        }
        if (next != nullptr) {
            next->prev(prev);
        }
    }

    /* Break a linked list at the specified nodes. */
    inline static void split(DoubleNode* prev, DoubleNode* curr) noexcept {
        if (prev != nullptr) {
            prev->next(nullptr);
        }
        if (curr != nullptr) {
            curr->prev(nullptr);
        }
    }

    /* Join the list at the specified nodes. */
    inline static void join(DoubleNode* prev, DoubleNode* curr) noexcept {
        if (prev != nullptr) {
            prev->next(curr);
        }
        if (curr != nullptr) {
            curr->prev(prev);
        }
    }

};


/* A node decorator that computes a key function on a node's underlying value
for use in sorting algorithms.

NOTE: this is a special case of node used in the `sort()` method to apply a key
function to each value in a list.  It is not meant to be used in any other context. */
template <
    typename Wrapped,
    typename Func,
    typename DecoratedValue = typename bertrand::util::FuncTraits<
        Func,
        typename Wrapped::Value
    >::ReturnType
>
class Keyed : public SingleNode<DecoratedValue> {
private:
    using Base = SingleNode<DecoratedValue>;
    using ArgValue = typename Wrapped::Value;
    Wrapped* _node;  // reference to decorated node

    /* Invoke the key function on the specified value and return the computed result. */
    static DecoratedValue invoke(Func func, const ArgValue& arg) {
        if constexpr (is_pyobject<Func>) {
            static_assert(
                is_pyobject<typename Base::Value>,
                "Python functions can only be applied to PyObject* nodes"
            );
            PyObject* val = PyObject_CallFunctionObjArgs(func, arg, nullptr);
            if (val == nullptr) {
                throw catch_python();
            }
            return val;  // new reference
        } else {
            return func(arg);
        }
    }

public:
    using Value = DecoratedValue;
    static constexpr bool doubly_linked = false;

    /* Initialize a keyed node by applying a Python callable to an existing node. */
    Keyed(Wrapped* node, Func func) :
        Base(invoke(func, node->value())), _node(node)
    {}

    /* Copy constructor/assignment disabled due to presence of raw Node* pointer and as
    a safeguard against unnecessary copies in sort algorithms. */
    Keyed(const Keyed& other) = delete;
    Keyed& operator=(const Keyed& other) = delete;

    /* Move constructor. */
    Keyed(Keyed&& other) noexcept : Base(std::move(other)), _node(other._node) {
        other._node = nullptr;
    }

    /* Move assignment operator. */
    Keyed& operator=(Keyed&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        Base::operator=(other);
        _node = other._node;
        other._node = nullptr;
        return *this;
    }

    /* Destroy a keyed decorator and release its resources. */
    ~Keyed() noexcept {
        _node = nullptr;
    }

    /* Get the decorated node. */
    inline Wrapped* node() const noexcept {
        return _node;
    }

    /* Get the next node in the list. */
    inline Keyed* next() const noexcept {
        return static_cast<Keyed*>(Base::next());
    }

    /* Set the next node in the list. */
    inline void next(Keyed* next) noexcept {
        Base::next(next);
    }

    /* Link the node to its neighbors to form a doubly-linked list. */
    inline static void link(Keyed* prev, Keyed* curr, Keyed* next) noexcept {
        Base::link(prev, curr, next);
    }

    /* Unlink the node from its neighbors. */
    inline static void unlink(Keyed* prev, Keyed* curr, Keyed* next) noexcept {
        Base::unlink(prev, curr, next);
    }

    /* Break a linked list at the specified nodes. */
    inline static void split(Keyed* prev, Keyed* curr) noexcept {
        Base::split(prev, curr);
    }

    /* Join the list at the specified nodes. */
    inline static void join(Keyed* prev, Keyed* curr) noexcept {
        Base::join(prev, curr);
    }

};


///////////////////////////////
////    NODE DECORATORS    ////
//////////////////////////////


/* A node decorator that computes the hash of the underlying PyObject* and
caches it alongside the node's original fields. */
template <typename Wrapped>
class Hashed : public Wrapped {
    size_t _hash;

    /* Compute the hash of the underlying node value. */
    inline static size_t compute_hash(Hashed* node) {
        try {
            return bertrand::hash(node->value());
        } catch (...) {
            // NOTE: we have to make sure to release any resources that were
            // acquired during the wrapped constructor
            node->~Hashed();
            throw;
        }
    }

public:

    /* Delegate to the wrapped node constructor. */
    template <typename... Args>
    Hashed(Args... args) :
        Wrapped(std::forward<Args>(args)...), _hash(compute_hash(this))
    {}

    /* Copy constructor. */
    Hashed(const Hashed& other) noexcept : Wrapped(other), _hash(other._hash) {}

    /* Move constructor. */
    Hashed(Hashed&& other) noexcept : Wrapped(std::move(other)), _hash(other._hash) {}

    /* Copy assignment operator. */
    Hashed& operator=(const Hashed& other) {
        if (this == &other) {
            return *this;
        }
        Wrapped::operator=(other);
        _hash = other._hash;
        return *this;
    }

    /* Move assignment operator. */
    Hashed& operator=(Hashed&& other) {
        if (this == &other) {
            return *this;
        }
        Wrapped::operator=(other);
        _hash = other._hash;
        return *this;
    }

    /* Get the hash of the node's value. */
    inline size_t hash() const noexcept {
        return _hash;
    }

    /* Get the next node in the list. */
    inline Hashed* next() const noexcept {
        return static_cast<Hashed*>(Wrapped::next());
    }

    /* Set the next node in the list. */
    inline void next(Hashed* next) noexcept {
        Wrapped::next(next);
    }

    /* Get the previous node in the list. */
    template <typename T = Wrapped, std::enable_if_t<T::doubly_linked, int> = 0>
    inline Hashed* prev() const noexcept {
        return static_cast<Hashed*>(T::prev());
    }

    /* Set the previous node in the list. */
    template <typename T = Wrapped, std::enable_if_t<T::doubly_linked, int> = 0>
    inline void prev(Hashed* prev) noexcept {
        T::prev(prev);
    }

    /* Link the node to its neighbors to form a doubly-linked list. */
    inline static void link(Hashed* prev, Hashed* curr, Hashed* next) noexcept {
        Wrapped::link(prev, curr, next);
    }

    /* Unlink the node from its neighbors. */
    inline static void unlink(Hashed* prev, Hashed* curr, Hashed* next) noexcept {
        Wrapped::unlink(prev, curr, next);
    }

    /* Break a linked list at the specified nodes. */
    inline static void split(Hashed* prev, Hashed* curr) noexcept {
        Wrapped::split(prev, curr);
    }

    /* Join the list at the specified nodes. */
    inline static void join(Hashed* prev, Hashed* curr) noexcept {
        Wrapped::join(prev, curr);
    }

};


/* A node decorator that attaches a second value to each node, allowing the list to act
as a dictionary. */
template <typename Wrapped, typename MappedType>
class Mapped : public Wrapped {
    using KeyType = typename Wrapped::Value;
    MappedType _mapped;

    /* Unpack a python tuple containing a key and value. */
    inline static std::pair<PyObject*, PyObject*> unpack_python(const PyObject* tuple) {
        static_assert(
            is_pyobject<KeyType> && is_pyobject<MappedType>,
            "Python tuples can only be unpacked by PyObject* nodes"
        );

        if (!PyTuple_Check(tuple) || PyTuple_Size(tuple) != 2) {
            std::ostringstream msg;
            msg << "Expected tuple of size 2 (key, value), not: " << repr(tuple);
            throw TypeError(msg.str());
        }

        PyObject* key = PyTuple_GET_ITEM(tuple, 0);
        PyObject* mapped = PyTuple_GET_ITEM(tuple, 1);
        return std::make_pair(key, mapped);
    }

public:
    using MappedValue = MappedType;

    /* Initialize a mapped node with a separate key and value. */
    Mapped(const KeyType& key, const MappedValue& value) : Wrapped(key), _mapped(value) {
        if constexpr (is_pyobject<MappedType>) {
            Py_XINCREF(value);
        }
    }

    /* Initialize a mapped node with a coupled key and value. */
    Mapped(const std::pair<KeyType, MappedValue>& pair) : Mapped(pair.first, pair.second) {}
    Mapped(const std::tuple<KeyType, MappedValue>& tuple) :
        Mapped(std::get<0>(tuple), std::get<1>(tuple))
    {}
    Mapped(const PyObject* tuple) : Mapped(unpack_python(tuple)) {}

    /* Copy constructor. */
    Mapped(const Mapped& other) noexcept : Wrapped(other), _mapped(other._mapped) {
        if constexpr (is_pyobject<MappedType>) {
            Py_XINCREF(_mapped);
        }
    }

    /* Move constructor. */
    Mapped(Mapped&& other) noexcept :
        Wrapped(std::move(other)), _mapped(std::move(other._mapped))
    {
        if constexpr (std::is_pointer_v<MappedValue>) {
            other._mapped = nullptr;
        }
    }

    /* Copy assignment operator. */
    Mapped& operator=(const Mapped& other) noexcept {
        if (this == &other) {
            return *this;
        }
        Wrapped::operator=(other);
        _mapped = other._mapped;
        if constexpr (is_pyobject<MappedType>) {
            Py_XINCREF(_mapped);
        }
        return *this;
    }

    /* Move assignment operator. */
    Mapped& operator=(Mapped&& other) noexcept {
        if (this == &other) {
            return *this;
        }
        Wrapped::operator=(other);
        _mapped = std::move(other._mapped);
        if constexpr (std::is_pointer_v<MappedValue>) {
            other._mapped = nullptr;
        }
        return *this;
    }

    /* Destroy a mapped node and release its resources. */
    ~Mapped() noexcept {
        if constexpr (is_pyobject<MappedType>) {
            Py_XDECREF(_mapped);
        }
    }

    /* Get the mapped value. */
    inline MappedValue mapped() const noexcept {
        return _mapped;
    }

    /* Overwrite the mapped value. */
    inline void mapped(MappedValue&& mapped) noexcept {
        if constexpr (is_pyobject<MappedType>) {
            Py_XDECREF(_mapped);
            Py_XINCREF(mapped);
        }
        _mapped = std::forward<MappedValue>(mapped);
    }

    /* Get the next node in the list. */
    inline Mapped* next() const noexcept {
        return static_cast<Mapped*>(Wrapped::next());
    }

    /* Set the next node in the list. */
    inline void next(Mapped* next) noexcept {
        Wrapped::next(next);
    }

    /* Get the previous node in the list. */
    template <typename T = Wrapped, std::enable_if_t<T::doubly_linked, int> = 0>
    inline Mapped* prev() const noexcept {
        return static_cast<Mapped*>(T::prev());
    }

    /* Set the previous node in the list. */
    template <typename T = Wrapped, std::enable_if_t<T::doubly_linked, int> = 0>
    inline void prev(Mapped* prev) noexcept {
        T::prev(prev);
    }

    /* Link the node to its neighbors to form a doubly-linked list. */
    inline static void link(Mapped* prev, Mapped* curr, Mapped* next) noexcept {
        Wrapped::link(prev, curr, next);
    }

    /* Unlink the node from its neighbors. */
    inline static void unlink(Mapped* prev, Mapped* curr, Mapped* next) noexcept {
        Wrapped::unlink(prev, curr, next);
    }

    /* Break a linked list at the specified nodes. */
    inline static void split(Mapped* prev, Mapped* curr) noexcept {
        Wrapped::split(prev, curr);
    }

    /* Join the list at the specified nodes. */
    inline static void join(Mapped* prev, Mapped* curr) noexcept {
        Wrapped::join(prev, curr);
    }

    /* Allow specialization of both the key and value of a mapped node. */
    template <bool cond = is_pyobject<typename Wrapped::Value>>
    inline std::enable_if_t<cond, bool> typecheck(PyObject* specialization) const {
        if (specialization == nullptr) {
            return true;
        }

        if (PySlice_Check(specialization)) {
            PyObject* start = PyObject_GetAttrString(specialization, "start");
            if (start == nullptr) {
                throw catch_python();
            }
            PyObject* stop = PyObject_GetAttrString(specialization, "stop");
            if (stop == nullptr) {
                Py_DECREF(start);
                throw catch_python();
            }

            // start index corresponds to key type
            if (start != Py_None) {
                int comp = PyObject_IsInstance(this->value(), start);
                if (comp == -1) {
                    Py_DECREF(start);
                    Py_DECREF(stop);
                    throw catch_python();
                }
                if (!comp) {
                    Py_DECREF(start);
                    Py_DECREF(stop);
                    return false;
                }
            }

            // stop index corresponds to value type
            if (stop != Py_None) {
                int comp = PyObject_IsInstance(_mapped, stop);
                if (comp == -1) {
                    Py_DECREF(start);
                    Py_DECREF(stop);
                    throw catch_python();
                }
                if (!comp) {
                    Py_DECREF(start);
                    Py_DECREF(stop);
                    return false;
                }
            }

            Py_DECREF(start);
            Py_DECREF(stop);
            return true;
        }

        // fall back to only checking keys
        return Wrapped::typecheck(specialization);
    }

};


//////////////////////
////    TRAITS    ////
//////////////////////


/* A collection of SFINAE traits for inspecting node types at compile time. */
template <typename NodeType>
class NodeTraits {
    // sanity check
    static_assert(
        std::is_base_of_v<NodeTag, NodeType>,
        "Templated type does not inherit from BaseNode"
    );

    /* Detects whether the templated type has a prev() method. */
    struct _has_prev {
        template <typename T>
        static constexpr auto test(T* t) -> decltype(t->prev(), std::true_type());
        template <typename T>
        static constexpr auto test(...) -> std::false_type;
        static constexpr bool value = decltype(test<NodeType>(nullptr))::value;
    };

    /* Detects whether the templated type has a node() method. */
    struct _has_node {
        template <typename T>
        static constexpr auto test(T* t) -> decltype(t->node(), std::true_type());
        template <typename T>
        static constexpr auto test(...) -> std::false_type;
        static constexpr bool value = decltype(test<NodeType>(nullptr))::value;
    };

    /* Detects whether the templated type has a hash() method. */
    struct _has_hash {
        template <typename T>
        static constexpr auto test(T* t) -> decltype(t->hash(), std::true_type());
        template <typename T>
        static constexpr auto test(...) -> std::false_type;
        static constexpr bool value = decltype(test<NodeType>(nullptr))::value;
    };

    /* Detects whether the templated type has a mapped() accessor. */
    struct _has_mapped {
        template <typename T>
        static constexpr auto test(T* t) -> decltype(t->mapped(), std::true_type());
        template <typename T>
        static constexpr auto test(...) -> std::false_type;
        static constexpr bool value = decltype(test<NodeType>(nullptr))::value;
    };

public:
    using Value = decltype(std::declval<NodeType>().value());

    static constexpr bool has_prev = _has_prev::value;
    static constexpr bool has_node = _has_node::value;
    static constexpr bool has_hash = _has_hash::value;
    static constexpr bool has_mapped = _has_mapped::value;
};


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_CORE_NODE_H
