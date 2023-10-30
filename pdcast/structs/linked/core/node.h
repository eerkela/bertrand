// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_NODE_H
#define BERTRAND_STRUCTS_CORE_NODE_H

#include <queue>  // std::queue
#include <mutex>  // std::mutex
#include <sstream>  // std::ostringstream
#include <stdexcept>  // std::invalid_argument
#include <type_traits>  // std::enable_if_t<>, std::is_convertible_v<>, etc.
#include <Python.h>  // CPython API
#include "../util/except.h"  // catch_python(), type_error()
#include "../util/func.h"  // ReturnType
#include "../util/repr.h"  // repr()


namespace bertrand {
namespace structs {
namespace linked {


////////////////////
////    BASE    ////
////////////////////


/* Empty tag class marking a node for a linked data structure.

NOTE: this class is inherited by all nodes, and can be used for easy SFINAE checks via
std::is_base_of, without requiring any foreknowledge of template parameters. */
class NodeTag {};


/* Base class containing common functionality across all nodes. */
template <typename ValueType>
class BaseNode : public NodeTag {
    inline static constexpr bool py_val = std::is_convertible_v<ValueType, PyObject*>;
    ValueType _value;

public:
    using Value = ValueType;

    /* Get the value within the node. */
    inline Value value() const noexcept {
        return _value;
    }

    /* Apply a less-than comparison to the wrapped value. */
    inline bool lt(Value other) const {
        if constexpr (py_val) {
            int comp = PyObject_RichCompareBool(_value, other, Py_LT);
            if (comp == -1) {  // error during comparison
                throw util::catch_python<util::type_error>();
            }
            return static_cast<bool>(comp);
        }
        return _value < other;
    }

    /* Apply a less-than-or-equal comparison to the wrapped value. */
    inline bool le(Value other) const {
        if constexpr (py_val) {
            int comp = PyObject_RichCompareBool(_value, other, Py_LE);
            if (comp == -1) {  // error during comparison
                throw util::catch_python<util::type_error>();
            }
            return static_cast<bool>(comp);
        }
        return _value <= other;
    }

    /* Apply an equality comparison to the wrapped value. */
    inline bool eq(Value other) const {
        if constexpr (py_val) {
            int comp = PyObject_RichCompareBool(_value, other, Py_EQ);
            if (comp == -1) {  // error during comparison
                throw util::catch_python<util::type_error>();
            }
            return static_cast<bool>(comp);
        }
        return _value == other;
    }

    /* Apply an inequality comparison to the wrapped value. */
    inline bool ne(Value other) const {
        if constexpr (py_val) {
            int comp = PyObject_RichCompareBool(_value, other, Py_NE);
            if (comp == -1) {  // error during comparison
                throw util::catch_python<util::type_error>();
            }
            return static_cast<bool>(comp);
        }
        return _value != other;
    }

    /* Apply a greater-than-or-equal comparison to the wrapped value. */
    inline bool ge(Value other) const {
        if constexpr (py_val) {
            int comp = PyObject_RichCompareBool(_value, other, Py_GE);
            if (comp == -1) {  // error during comparison
                throw util::catch_python<util::type_error>();
            }
            return static_cast<bool>(comp);
        }
        return _value >= other;
    }

    /* Apply a greater-than comparison to the wrapped value. */
    inline bool gt(Value other) const {
        if constexpr (py_val) {
            int comp = PyObject_RichCompareBool(_value, other, Py_GT);
            if (comp == -1) {  // error during comparison
                throw util::catch_python<util::type_error>();
            }
            return static_cast<bool>(comp);
        }
        return _value > other;
    }

    /* Apply an explicit type check to the wrapped value if it is a Python object. */
    template <bool cond = py_val>
    inline std::enable_if_t<cond, bool> typecheck(PyObject* specialization) const {
        int comp = PyObject_IsInstance(_value, specialization);
        if (comp == -1) {
            throw util::catch_python<util::type_error>();
        }
        return static_cast<bool>(comp);
    }

protected:

    /* Initialize a node with a given value. */
    BaseNode(Value value) noexcept : _value(value) {
        if constexpr (py_val) {
            Py_XINCREF(value);
        }
    }

    /* Copy constructor. */
    BaseNode(const BaseNode& other) noexcept : _value(other._value) {
        if constexpr (py_val) {
            Py_XINCREF(_value);
        }
    }

    /* Move constructor. */
    BaseNode(BaseNode&& other) noexcept : _value(std::move(other._value)) {
        if constexpr (std::is_pointer_v<Value>) {
            other._value = nullptr;
        }
    }

    /* Copy assignment operator. */
    BaseNode& operator=(const BaseNode& other) noexcept {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // clear current node
        if constexpr (py_val) {
            Py_XDECREF(_value);
        }

        // copy other node
        _value = other._value;
        if constexpr (py_val) {
            Py_XINCREF(_value);
        }
        return *this;
    }

    /* Move assignment operator. */
    BaseNode& operator=(BaseNode&& other) noexcept {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // clear current node
        if constexpr (py_val) {
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
    ~BaseNode() noexcept {
        if constexpr (py_val) {
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
    SingleNode(Value value) noexcept : Base(value), _next(nullptr) {}

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
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // copy value from other node
        Base::operator=(other);

        // clear current node's next pointer
        _next = nullptr;
        return *this;
    }

    /* Move assignment operator. */
    SingleNode& operator=(SingleNode&& other) noexcept {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // move value from other node
        Base::operator=(other);

        // move next pointer from other node
        _next = other._next;
        other._next = nullptr;
        return *this;
    }

    /* Destroy a singly-linked node and release its resources. */
    ~SingleNode() noexcept {
        _next = nullptr;  // Base::~Base() releases _value
    }

    /* Get the next node in the list. */
    inline SingleNode* next() const noexcept {
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
        curr->next(nullptr);
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
    DoubleNode(Value value) noexcept : Base(value), _prev(nullptr) {}

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
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // copy value from other node and clear current node's next pointer
        Base::operator=(other);

        // clear current node's prev pointer
        _prev = nullptr;
        return *this;
    }

    /* Move assignment operator. */
    DoubleNode& operator=(DoubleNode&& other) {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // move value/next from other node
        Base::operator=(other);

        // move prev pointer from other node
        _prev = other._prev;
        other._prev = nullptr;
        return *this;
    }

    /* Destroy a doubly-linked node and release its resources. */
    ~DoubleNode() noexcept {
        _prev = nullptr;  // Base::~Base() releases _value/_next
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
    typename _Value = typename util::FuncTraits<Func, typename Wrapped::Value>::ReturnType
>
class Keyed : public SingleNode<_Value> {
private:
    using Base = SingleNode<_Value>;
    Wrapped* _node;  // reference to decorated node

    /* Invoke the key function on the specified value and return the computed result. */
    _Value invoke(Func func, typename Wrapped::Value arg) {
        // Python key
        if constexpr (std::is_convertible_v<Func, PyObject*>) {
            static_assert(
                std::is_convertible_v<typename Base::Value, PyObject*>,
                "Python functions can only be applied to PyObject* nodes"
            );

            // apply key function to node value
            PyObject* val = PyObject_CallFunctionObjArgs(func, arg, nullptr);
            if (val == nullptr) {
                throw util::catch_python<util::type_error>();
            }
            return val;  // new reference

        // C++ key
        } else {
            return func(arg);
        }
    }

public:
    using Value = _Value;
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
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // move value from other node
        Base::operator=(other);

        // move node pointer from other node
        _node = other._node;
        other._node = nullptr;
        return *this;
    }

    /* Destroy a keyed decorator and release its resources. */
    ~Keyed() noexcept {
        _node = nullptr;  // Base::~Base() releases _value/_next
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
        using Value = typename Wrapped::Value;

        if constexpr (std::is_convertible_v<Value, PyObject*>) {
            Py_hash_t hash_val = PyObject_Hash(node->value());
            if (hash_val == -1 && PyErr_Occurred()) {
                // NOTE: we have to make sure to release any resources that were
                // acquired during the wrapped constructor
                node->~Hashed();
                throw util::catch_python<util::type_error>();
            }
            return static_cast<size_t>(hash_val);  // for compatibility with std::hash
        } else {
            return std::hash<Value>()(node->value());
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
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // copy wrapped node
        Wrapped::operator=(other);

        // copy hash
        _hash = other._hash;
        return *this;
    }

    /* Move assignment operator. */
    Hashed& operator=(Hashed&& other) {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // move wrapped node
        Wrapped::operator=(other);

        // move hash
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
template <typename Wrapped, typename MappedType = PyObject*>
class Mapped : public Wrapped {
    using KeyType = typename Wrapped::Value;
    static constexpr bool py_mapped = std::is_convertible_v<MappedType, PyObject*>;
    MappedType _mapped;

    /* Unpack a python tuple containing a key and value. */
    inline static std::pair<PyObject*, PyObject*> unpack_python(PyObject* tuple) {
        static constexpr bool py_key = std::is_convertible_v<KeyType, PyObject*>;
        static constexpr bool py_value = std::is_convertible_v<MappedType, PyObject*>;
        static_assert(
            py_key && py_value,
            "Python tuples can only be unpacked by PyObject* nodes"
        );

        // Check that item is a tuple of size 2 (key-value pair)
        if (!PyTuple_Check(tuple) || PyTuple_Size(tuple) != 2) {
            std::ostringstream msg;
            msg << "Expected tuple of size 2 (key, value), not: " << util::repr(tuple);
            throw util::type_error(msg.str());
        }

        // unpack tuple and return pair
        PyObject* key = PyTuple_GetItem(tuple, 0);
        PyObject* mapped = PyTuple_GetItem(tuple, 1);
        return std::make_pair(key, mapped);
    }

public:
    using MappedValue = MappedType;

    /* Initialize a mapped node with a separate key and value. */
    Mapped(KeyType key, MappedValue value) : Wrapped(key), _mapped(value) {
        if constexpr (py_mapped) {
            Py_INCREF(value);
        }
    }

    /* Initialize a mapped node with a coupled key and value. */
    Mapped(std::pair<KeyType, MappedValue> pair) : Mapped(pair.first, pair.second) {}
    Mapped(std::tuple<KeyType, MappedValue> tuple) :
        Mapped(std::get<0>(tuple), std::get<1>(tuple))
    {}
    Mapped(PyObject* tuple) : Mapped(unpack_python(tuple)) {}

    /* Copy constructor. */
    Mapped(const Mapped& other) noexcept : Wrapped(other), _mapped(other._mapped) {
        if constexpr (py_mapped) {
            Py_INCREF(_mapped);
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
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // copy wrapped node
        Wrapped::operator=(other);

        // copy mapped value
        _mapped = other._mapped;
        if constexpr (py_mapped) {
            Py_XINCREF(_mapped);
        }
        return *this;
    }

    /* Move assignment operator. */
    Mapped& operator=(Mapped&& other) noexcept {
        // check for self-assignment
        if (this == &other) {
            return *this;
        }

        // move wrapped node
        Wrapped::operator=(other);

        // move mapped value
        _mapped = std::move(other._mapped);
        if constexpr (std::is_pointer_v<MappedValue>) {
            other._mapped = nullptr;
        }
        return *this;
    }

    /* Destroy a mapped node and release its resources. */
    ~Mapped() noexcept {
        if constexpr (py_mapped) {
            Py_XDECREF(_mapped);
        }
    }

    /* Get the mapped value. */
    inline MappedValue mapped() const noexcept {
        return _mapped;
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

};




// TODO: Threaded nodes are more trouble than they're worth.  Just add a different
// LockPolicy that uses a shared_mutex instead.


/* A node decorator that adds a separate mutex to each node, for use in heavily
multithreaded contexts. */
template <typename Wrapped>
struct Threaded : public Wrapped {
    using Node = Threaded<Wrapped>;

    mutable std::mutex mutex;

    // NOTE: the idea here is that we implement standard next()/prev() getters
    // on all nodes.  For most nodes, these just cast the associated pointer to
    // the correct type and return it.  For threaded nodes, however, we also
    // lock the node's mutex before returning the pointer.  This allows us to
    // abstract away the mutex locking/unlocking from the user.

    // This would be paired with head() and tail() accessors on the list itself,
    // which do the same thing.  Same with the search() method.

    // Since these methods encompass the entire traversal mechanism for the list,
    // we can effectively automate the locking/unlocking process and rely on
    // polymorphism to handle it for us.

    // We would still have to put in some constexpr checks to make sure that
    // nodes are properly unlocked after we're done with them, but otherwise,
    // this would create a linked list where every node is individually locked
    // and unlocked, which would allow concurrent access to several different
    // parts of the list at the same time.

    // NOTE: Here's how the accessors would work:
    // - calling the accessor without arguments would act as a getter
    // - calling the accessor with arguments would act as a setter

    // Node* curr = view->head();
    // Node* next = curr->next();

    // Node* temp = view->node(item);
    // curr->next(temp);  // assigns temp to curr->next  

    /* Access the next node in the list. */
    inline Node* next() {
        Node* result = static_cast<Node*>(Wrapped::next());
        if (result != nullptr) {
            result->mutex.lock();  // lock the next node's mutex on access
        }
        return result;
    }

    // NOTE: prev() is conditionally compiled based on whether the templated node type
    // is doubly-linked (i.e. has a prev() method).  This is done using SFINAE.

    /* Access the previous node in the list. */
    inline auto prev() -> std::enable_if_t<
        std::is_same_v<decltype(std::declval<Wrapped>().prev()), Wrapped*>,
        Node*
    > {
        Node* result = static_cast<Node*>(Wrapped::prev());
        if (result != nullptr) {
            result->mutex.lock();  // lock the previous node's mutex on access
        }
        return result;
    }

    /* Unlock this node's mutex. */
    inline void unlock() {
        mutex.unlock();
    }

};


/* A node decorator that adds a frequency count to the underyling node type. */
template <typename Wrapped>
struct Counted : public Wrapped {
    size_t frequency;

    /* Initialize a newly-allocated node. */
    inline static Counted<Wrapped>* init(Counted<Wrapped>* node, PyObject* value) {
        node = (Counted<Wrapped>*)Wrapped::init(node, value);
        if (node == nullptr) {  // Error during decorated init()
            return nullptr;  // propagate
        }

        // initialize frequency
        node->frequency = 1;

        // return initialized node
        return node;
    }

    /* Initialize a copied node. */
    inline static Counted<Wrapped>* init_copy(
        Counted<Wrapped>* new_node,
        Counted<Wrapped>* old_node
    ) {
        // delegate to templated init_copy() method
        new_node = (Counted<Wrapped>*)Wrapped::init_copy(new_node, old_node);
        if (new_node == nullptr) {  // Error during templated init_copy()
            return nullptr;  // propagate
        }

        // copy frequency
        new_node->frequency = old_node->frequency;
        return new_node;
    }

    /* Tear down a node before freeing it. */
    inline static void teardown(Counted<Wrapped>* node) {
        node->frequency = 0; // reset frequency
        Wrapped::teardown(node);
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

    /* Detects whether the templated type has a count() accessor. */
    struct _has_count {
        template <typename T>
        static constexpr auto test(T* t) -> decltype(t->count(), std::true_type());
        template <typename T>
        static constexpr auto test(...) -> std::false_type;
        static constexpr bool value = decltype(test<NodeType>(nullptr))::value;
    };

public:
    using Value = decltype(std::declval<NodeType>().value());

    // TODO: has_pyobject
    static constexpr bool has_prev = _has_prev::value;
    static constexpr bool has_node = _has_node::value;
    static constexpr bool has_hash = _has_hash::value;
    static constexpr bool has_mapped = _has_mapped::value;
    static constexpr bool has_count = _has_count::value;

    /*
    NOTE: when GCC accepts C++20 as the default standard, we can replace
    all of the above with:

    inline static constexpr bool has_prev = requires(Node node) {
        { node.prev() } -> std::convertible_to<Node*>;
    };
    inline static constexpr bool has_hash = requires(Node node) {
        { node.hash() } -> std::convertible_to<Py_hash_t>;
    };
    inline static constexpr bool has_mapped = requires(Node node) {
        { node.mapped() } -> std::convertible_to<PyObject*>;
    };
    inline static constexpr bool has_mutex = requires(Node node) {
        { node.mutex() } -> std::convertible_to<std::mutex&>;
    };
    */

};


}  // namespace linked
}  // namespace structs
}  // namespace bertrand


#endif // BERTRAND_STRUCTS_CORE_NODE_H include guard
