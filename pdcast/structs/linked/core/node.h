#ifndef BERTRAND_STRUCTS_LINKED_CORE_NODE_H
#define BERTRAND_STRUCTS_LINKED_CORE_NODE_H

#include <sstream>  // std::ostringstream
#include <type_traits>  // std::enable_if_t<>, etc.
#include <Python.h>  // CPython API
#include "../../util/base.h"  // is_pyobject<>
#include "../../util/container.h"  // python::Slice, python::Function
#include "../../util/except.h"  // catch_python(), TypeError()
#include "../../util/func.h"  // FuncTraits
#include "../../util/ops.h"  // hash(), repr()


namespace bertrand {
namespace linked {


template <typename NodeType>
class NodeTraits;


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
    inline Value& value() noexcept {
        return _value;
    }

    /* Get the value held by the node. */
    inline const Value& value() const noexcept {
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
        if constexpr (is_pyobject<Value>) {
            Py_XDECREF(_value);
        }
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
        if constexpr (is_pyobject<Value>) {
            Py_XDECREF(_value);
        }
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
protected:
    using Base = BaseNode<ValueType>;
    SingleNode* _next;

    template <typename NodeType>
    friend class NodeTraits;

    using Root = SingleNode;
    using Unwrap = SingleNode;

    template <typename... Args>
    using Reconfigure = SingleNode<Args...>;

public:
    using Value = ValueType;

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
    SingleNode* next() noexcept {
        return _next;
    }

    const SingleNode* next() const noexcept {
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
protected:
    using Base = SingleNode<ValueType>;
    DoubleNode* _prev;

    template <typename NodeType>
    friend class NodeTraits;

    using Root = DoubleNode;
    using Unwrap = DoubleNode;

    template <typename... Args>
    using Reconfigure = DoubleNode<Args...>;

public:
    using Value = ValueType;

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
    inline DoubleNode* next() noexcept {
        return static_cast<DoubleNode*>(Base::next());
    }

    /* Get the next node in the list. */
    inline const DoubleNode* next() const noexcept {
        return static_cast<const DoubleNode*>(Base::next());
    }

    /* Set the next node in the list. */
    inline void next(DoubleNode* next) noexcept {
        Base::next(next);
    }

    /* Get the previous node in the list. */
    inline DoubleNode* prev() noexcept {
        return _prev;
    }

    /* Get the previous node in the list. */
    inline const DoubleNode* prev() const noexcept {
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


///////////////////////////////
////    NODE DECORATORS    ////
///////////////////////////////


namespace util {

    /* Extracts the return type of a key function when applied to a basic node. */
    template <typename Wrapped, typename Func, bool dictlike = false>
    struct KeyFunc_Return {
        using type = typename bertrand::util::FuncTraits<
            Func, typename Wrapped::Value
        >::ReturnType;
    };

    /* Extracts the return type of a key function when applied to a dictlike node. */
    template <typename Wrapped, typename Func>
    struct KeyFunc_Return<Wrapped, Func, true> {
        using type = typename bertrand::util::FuncTraits<
            Func, typename Wrapped::Value, typename Wrapped::MappedValue
        >::ReturnType;
    };

    template <typename Wrapped, typename Func>
    using KeyFunc_Return_t = typename KeyFunc_Return<
        Wrapped, Func, NodeTraits<Wrapped>::has_mapped
    >::type;

}


/* A node decorator that computes a key function on a node's underlying value
for use in sorting algorithms.  NOTE: this is a special case of node used in the
`sort()` method to apply a key function to each node in a list.  It is not meant to be
used in any other context. */
template <typename Wrapped, typename Func>
class Keyed : public SingleNode<util::KeyFunc_Return_t<Wrapped, Func>> {
protected:
    using Base = SingleNode<util::KeyFunc_Return_t<Wrapped, Func>>;

    Wrapped* _node;

    /* Invoke the key function on the specified value and return the computed result. */
    static typename Base::Value invoke(Func func, Wrapped* node) {
        if constexpr (is_pyobject<Func>) {
            python::Function<python::Ref::BORROW> pyfunc(func);
            if (pyfunc.n_args() < 1) {
                throw TypeError("key function must accept at least one argument");
            }

            if constexpr (NodeTraits<Wrapped>::has_mapped) {
                if (pyfunc.n_args() == 1) {
                    return pyfunc(node->value()).unwrap();
                } else if (pyfunc.n_args() == 2) {
                    return pyfunc(node->value(), node->mapped()).unwrap();
                } else {
                    throw TypeError(
                        "key function must accept at most two arguments "
                        "(key, value)"
                    );
                }
            } else {
                if (pyfunc.n_args() == 1) {
                    return pyfunc(node->value()).unwrap();
                } else {
                    throw TypeError(
                        "key function must accept at most one argument"
                    );
                }
            }
        } else {
            if constexpr (NodeTraits<Wrapped>::has_mapped) {
                return func(node->value(), node->mapped());
            } else {
                return func(node->value());
            }
        }
    }

    template <typename NodeType>
    friend class NodeTraits;

    using Unwrapped = Base;

    template <typename... Args>
    using Reconfigure = Keyed<Args...>;

public:

    /* Initialize a keyed node by applying a callable to an existing node. */
    Keyed(Wrapped* node, Func func) : Base(invoke(func, node)), _node(node) {
        if constexpr (is_pyobject<typename Base::Value>) {
            Py_DECREF(this->value());  // release extra reference from invoke()
        }
    }

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
    inline Keyed* next() noexcept {
        return static_cast<Keyed*>(Base::next());
    }

    /* Get the next node in the list. */
    inline const Keyed* next() const noexcept {
        return static_cast<const Keyed*>(Base::next());
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


/* A node decorator that computes the hash of the underlying PyObject* and
caches it alongside the node's original fields. */
template <typename Wrapped>
class Hashed : public Wrapped {
protected:
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

    template <typename NodeType>
    friend class NodeTraits;

    using Unwrapped = Wrapped;

    template <typename... Args>
    using Reconfigure = Hashed<Args...>;

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
    inline Hashed* next() noexcept {
        return static_cast<Hashed*>(Wrapped::next());
    }

    /* Get the next node in the list. */
    inline const Hashed* next() const noexcept {
        return static_cast<const Hashed*>(Wrapped::next());
    }

    /* Set the next node in the list. */
    inline void next(Hashed* next) noexcept {
        Wrapped::next(next);
    }

    /* Get the previous node in the list. */
    template <bool cond = NodeTraits<Wrapped>::has_prev>
    inline std::enable_if_t<cond, Hashed*> prev() noexcept {
        return static_cast<Hashed*>(Wrapped::prev());
    }

    /* Get the previous node in the list. */
    template <bool cond = NodeTraits<Wrapped>::has_prev>
    inline std::enable_if_t<cond, const Hashed*> prev() const noexcept {
        return static_cast<const Hashed*>(Wrapped::prev());
    }

    /* Set the previous node in the list. */
    template <bool cond = NodeTraits<Wrapped>::has_prev>
    inline std::enable_if_t<cond, void> prev(Hashed* prev) noexcept {
        Wrapped::prev(prev);
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



// TODO: should apply typecheck to value whenever we overwrite it.  This is only
// applicable when the specialization is a slice.  Actually is this necessary?  We
// already check whenever a new node is created.  This check would only be necessary
// if the value is overwritten without creating a new node.  This would only ever
// happen in update() calls and similar, but probably not even there.  Still worth
// being aware of.


/* A node decorator that attaches a second value to each node, allowing the list to act
as a dictionary. */
template <typename Wrapped, typename MappedType>
class Mapped : public Wrapped {
protected:
    MappedType _mapped;

    /* Unpack a python tuple containing a key and value. */
    inline static std::pair<PyObject*, PyObject*> unpack_python(PyObject* item) {
        static_assert(
            is_pyobject<Value> && is_pyobject<MappedType>,
            "Python tuples can only be unpacked by PyObject* nodes"
        );

        PyObject* key;
        PyObject* value;
        if (PyTuple_Check(item) && PyTuple_GET_SIZE(item) == 2) {
            key = PyTuple_GET_ITEM(item, 0);
            value = PyTuple_GET_ITEM(item, 1);
        } else if (PyList_Check(item) && PyList_GET_SIZE(item) == 2) {
            key = PyList_GET_ITEM(item, 0);
            value = PyList_GET_ITEM(item, 1);
        } else {
            std::ostringstream msg;
            msg << "expected tuple of size 2 (key, value), not " << repr(item);
            throw TypeError(msg.str());
        }

        return std::make_pair(key, value);
    }

    template <typename NodeType>
    friend class NodeTraits;

    using Unwrapped = Wrapped;

    template <typename... Args>
    using Reconfigure = Mapped<Args...>;

public:
    using Value = typename Wrapped::Value;
    using MappedValue = MappedType;

    /* Initialize a mapped node with a separate key and value. */
    Mapped(const Value& key, const MappedValue& value) : Wrapped(key), _mapped(value) {
        if constexpr (is_pyobject<MappedType>) {
            Py_XINCREF(value);
        }
    }

    /* Initialize a mapped node with a std::pair holding a separate key and value. */
    Mapped(const std::pair<Value, MappedValue>& pair) :
        Mapped(pair.first, pair.second)
    {}

    /* Initialize a mapped node with a std::tuple holding a separate key and value. */
    Mapped(const std::tuple<Value, MappedValue>& tuple) :
        Mapped(std::get<0>(tuple), std::get<1>(tuple))
    {}

    /* Initialize a mapped node with a Python tuple or list holding a separate key and
    value. */
    Mapped(PyObject* item) : Mapped(unpack_python(item)) {}

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
    inline MappedValue& mapped() noexcept {
        return _mapped;
    }

    /* Get the mapped value for a const node. */
    inline const MappedValue& mapped() const noexcept {
        return _mapped;
    }

    /* Overwrite the mapped value. */
    inline void mapped(const MappedValue& mapped) noexcept {
        if constexpr (is_pyobject<MappedType>) {
            Py_XDECREF(_mapped);
            Py_XINCREF(mapped);
        }
        _mapped = mapped;
    }

    /* Overwrite the mapped value. */
    inline void mapped(MappedValue&& mapped) noexcept {
        if constexpr (is_pyobject<MappedType>) {
            Py_XDECREF(_mapped);
            Py_XINCREF(mapped);
        }
        _mapped = std::move(mapped);
    }

    /* Get the next node in the list. */
    inline Mapped* next() noexcept {
        return static_cast<Mapped*>(Wrapped::next());
    }

    /* Get the next node in the list. */
    inline const Mapped* next() const noexcept {
        return static_cast<const Mapped*>(Wrapped::next());
    }

    /* Set the next node in the list. */
    inline void next(Mapped* next) noexcept {
        Wrapped::next(next);
    }

    /* Get the previous node in the list. */
    template <bool cond = NodeTraits<Wrapped>::has_prev>
    inline std::enable_if_t<cond, Mapped*> prev() noexcept {
        return static_cast<Mapped*>(Wrapped::prev());
    }

    /* Get the previous node in the list. */
    template <bool cond = NodeTraits<Wrapped>::has_prev>
    inline std::enable_if_t<cond, const Mapped*> prev() const noexcept {
        return static_cast<const Mapped*>(Wrapped::prev());
    }

    /* Set the previous node in the list. */
    template <bool cond = NodeTraits<Wrapped>::has_prev>
    inline std::enable_if_t<cond, void> prev(Mapped* prev) noexcept {
        Wrapped::prev(prev);
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

        // account for slice specialization
        if (PySlice_Check(specialization)) {
            python::Slice<python::Ref::BORROW> slice(specialization);
            if (!slice.start().is(Py_None)) {
                int comp = PyObject_IsInstance(this->value(), slice.start());
                if (comp == -1) {
                    throw catch_python();
                }
                if (!comp) {
                    return false;
                }
            }
            if (!slice.stop().is(Py_None)) {
                int comp = PyObject_IsInstance(_mapped, slice.stop());
                if (comp == -1) {
                    throw catch_python();
                }
                if (!comp) {
                    return false;
                }
            }
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
    using Node = std::remove_cv_t<std::remove_reference_t<NodeType>>;
    static_assert(
        std::is_base_of_v<NodeTag, Node>,
        "Templated type does not inherit from BaseNode"
    );

    /* Extracts mapped type if applicable, otherwise defaults to void. */
    template <bool dictlike, typename Dummy = void>
    struct _MappedValue {
        using type = void;
    };
    template <typename Dummy>
    struct _MappedValue<true, Dummy> {
        using type = typename Node::MappedValue;
    };

    /* Detects whether the templated type has a prev() method. */
    struct _has_prev {
        template <typename T>
        static constexpr auto test(T* t) -> decltype(t->prev(), std::true_type());
        template <typename T>
        static constexpr auto test(...) -> std::false_type;
        static constexpr bool value = decltype(test<Node>(nullptr))::value;
    };

    /* Detects whether the templated type has a node() method. */
    struct _has_node {
        template <typename T>
        static constexpr auto test(T* t) -> decltype(t->node(), std::true_type());
        template <typename T>
        static constexpr auto test(...) -> std::false_type;
        static constexpr bool value = decltype(test<Node>(nullptr))::value;
    };

    /* Detects whether the templated type has a hash() method. */
    struct _has_hash {
        template <typename T>
        static constexpr auto test(T* t) -> decltype(t->hash(), std::true_type());
        template <typename T>
        static constexpr auto test(...) -> std::false_type;
        static constexpr bool value = decltype(test<Node>(nullptr))::value;
    };

    /* Detects whether the templated type has a mapped() accessor. */
    struct _has_mapped {
        template <typename T>
        static constexpr auto test(T* t) -> decltype(t->mapped(), std::true_type());
        template <typename T>
        static constexpr auto test(...) -> std::false_type;
        static constexpr bool value = decltype(test<Node>(nullptr))::value;
    };

public:
    using Value = decltype(std::declval<Node>().value());
    using MappedValue = typename _MappedValue<_has_mapped::value>::type;

    // SFINAE checks
    static constexpr bool has_prev = _has_prev::value;
    static constexpr bool has_node = _has_node::value;
    static constexpr bool has_hash = _has_hash::value;
    static constexpr bool has_mapped = _has_mapped::value;

    // decorator traversal
    using Root = typename Node::Root;
    using Unwrap = typename Node::Unwrap;
    static constexpr bool is_root = std::is_same_v<Node, Root>;

    // template reconfiguration
    template <typename... Args>
    using Reconfigure = typename Node::template Reconfigure<Args...>;

};


}  // namespace linked
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_LINKED_CORE_NODE_H
