// include guard prevents multiple inclusion
#ifndef BERTRAND_STRUCTS_CORE_NODE_H
#define BERTRAND_STRUCTS_CORE_NODE_H

#include <queue>  // for std::queue
#include <mutex>  // std::mutex
#include <stdexcept>  // for std::invalid_argument
#include <type_traits>  // for std::integral_constant, std::is_base_of_v
#include <Python.h>  // for CPython API

#include "util.h"  // for catch_python, type_error


//////////////////////////
////    BASE CLASS    ////
//////////////////////////


/* Base class containing common functionality across all nodes. */
template <typename ValueType>
class BaseNode {
    ValueType _value;

public:
    using Value = ValueType;
    inline static constexpr bool has_pyobject = std::is_same_v<Value, PyObject*>;

    /* Get the value within the node. */
    inline Value value() const noexcept {
        return _value;
    }

    /* Apply a less-than comparison to the wrapped value. */
    inline bool lt(Value other) const {
        if constexpr (has_pyobject) {
            int comp = PyObject_RichCompareBool(_value, other, Py_LT);
            if (comp == -1) {  // error during comparison
                throw catch_python<type_error>();
            }
            return static_cast<bool>(comp);
        }
        return _value < other;
    }

    /* Apply a less-than-or-equal comparison to the wrapped value. */
    inline bool le(Value other) const {
        if constexpr (has_pyobject) {
            int comp = PyObject_RichCompareBool(_value, other, Py_LE);
            if (comp == -1) {  // error during comparison
                throw catch_python<type_error>();
            }
            return static_cast<bool>(comp);
        }
        return _value <= other;
    }

    /* Apply an equality comparison to the wrapped value. */
    inline bool eq(Value other) const {
        if constexpr (has_pyobject) {
            int comp = PyObject_RichCompareBool(_value, other, Py_EQ);
            if (comp == -1) {  // error during comparison
                throw catch_python<type_error>();
            }
            return static_cast<bool>(comp);
        }
        return _value == other;
    }

    /* Apply an inequality comparison to the wrapped value. */
    inline bool ne(Value other) const {
        if constexpr (has_pyobject) {
            int comp = PyObject_RichCompareBool(_value, other, Py_NE);
            if (comp == -1) {  // error during comparison
                throw catch_python<type_error>();
            }
            return static_cast<bool>(comp);
        }
        return _value != other;
    }

    /* Apply a greater-than-or-equal comparison to the wrapped value. */
    inline bool gt(Value other) const {
        if constexpr (has_pyobject) {
            int comp = PyObject_RichCompareBool(_value, other, Py_GT);
            if (comp == -1) {  // error during comparison
                throw catch_python<type_error>();
            }
            return static_cast<bool>(comp);
        }
        return _value > other;
    }

    /* Apply a greater-than comparison to the wrapped value. */
    inline bool ge(Value other) const {
        if constexpr (has_pyobject) {
            int comp = PyObject_RichCompareBool(_value, other, Py_GE);
            if (comp == -1) {  // error during comparison
                throw catch_python<type_error>();
            }
            return static_cast<bool>(comp);
        }
        return _value >= other;
    }

    /* Apply an explicit type check to the wrapped value if it is a Python object. */
    template <bool cond = has_pyobject>
    inline std::enable_if_t<cond, bool> typecheck(PyObject* specialization) const {
        int comp = PyObject_IsInstance(_value, specialization);
        if (comp == -1) {
            throw catch_python<type_error>();
        }
        return static_cast<bool>(comp);
    }

protected:

    /* Initialize a node with a given value. */
    BaseNode(Value value) noexcept : _value(value) {
        if constexpr (has_pyobject) {
            Py_XINCREF(value);
        }
    }

    /* Copy constructor. */
    BaseNode(const BaseNode& other) noexcept : _value(other._value) {
        if constexpr (has_pyobject) {
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
        if constexpr (has_pyobject) {
            Py_XDECREF(_value);
        }

        // copy other node
        _value = other._value;
        if constexpr (has_pyobject) {
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
        if constexpr (has_pyobject) {
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
        if constexpr (has_pyobject) {
            Py_XDECREF(_value);
        }
        if constexpr (std::is_pointer_v<Value>) {
            _value = nullptr;
        }
    }

};


/* A conditional value type that infers the return type of a C++ function pointer. */
template <typename T, typename Func, typename = void>
struct _ConditionalType {
    using type = std::invoke_result_t<Func, T>;
};


/* A conditional valute type that represents the return type of a python callable. */
template <typename T, typename Func>
struct _ConditionalType<T, Func, std::enable_if_t<std::is_same_v<Func, PyObject*>>> {
    using type = PyObject*;
};


/* An accessor that returns the type deduced by a ConditionalValue trait. */
template <typename Func, typename NodeType, typename Value = typename NodeType::Value>
using ConditionalType = typename _ConditionalType<Value, Func>::type;


//////////////////////////
////    ROOT NODES    ////
//////////////////////////


/* A singly-linked list node around an arbitrary value. */
template <typename ValueType = PyObject*>
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
template <typename ValueType = PyObject*>
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
for use in sorting algorithms. */
template <typename NodeType, typename Func>
class Keyed : public SingleNode<ConditionalType<Func, NodeType>> {
public:
    // NOTE: this is a special case of node used in the `sort()` method to apply a key
    // function to each node's value.  It is not meant to be used in any other context.
    using Value = ConditionalType<Func, NodeType>;

private:
    using Base = SingleNode<Value>;  // stores the result of the key function
    NodeType* _node;  // reference to decorated node

    /* Invoke the key function on the specified value and return the computed result. */
    Value invoke(Func func, typename NodeType::Value arg) {
        // Python key
        if constexpr (std::is_same_v<Func, PyObject*>) {
            static_assert(
                Base::has_pyobject,
                "Python functions can only be applied to PyObject* nodes"
            );

            // apply key function to node value
            PyObject* val = PyObject_CallFunctionObjArgs(func, arg, nullptr);
            if (val == nullptr) {
                throw catch_python<type_error>();
            }
            return val;  // new reference

        // C++ key
        } else {
            return func(arg);
        }
    }

public:
    static constexpr bool doubly_linked = false;

    /* Initialize a keyed node by applying a Python callable to an existing node. */
    Keyed(NodeType* node, Func func) :
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
    inline NodeType* node() const noexcept {
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

    /* Link the node to its neighbors to form a singly-linked list. */
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
template <typename NodeType>
struct Hashed : public NodeType {
    using Node = Hashed<NodeType>;

    Py_hash_t hash;

    /* Initialize a newly-allocated node. */
    inline static Node* init(Node* node, PyObject* value) {
        node = static_cast<Node*>(NodeType::init(node, value));
        if (node == nullptr) {  // Error during decorated init()
            return nullptr;  // propagate
        }

        // compute hash
        node->hash = PyObject_Hash(value);
        if (node->hash == -1 && PyErr_Occurred()) {
            NodeType::teardown(node);  // free any resources allocated during init()
            return nullptr;  // propagate TypeError()
        }

        // return initialized node
        return node;
    }

    /* Initialize a copied node. */
    inline static Node* init_copy(Node* new_node, Node* old_node) {
        new_node = static_cast<Node*>(NodeType::init_copy(new_node, old_node));
        if (new_node == nullptr) {  // Error during decorated init_copy()
            return nullptr;  // propagate
        }

        // reuse the pre-computed hash
        new_node->hash = old_node->hash;
        return new_node;
    }

};


/* A node decorator that hashes the underlying object and adds a second
PyObject* reference, allowing the list to act as a dictionary. */
template <typename NodeType>
struct Mapped : public NodeType {
    using Node = Mapped<NodeType>;

    Py_hash_t hash;
    PyObject* mapped;

    /* Initialize a newly-allocated node (1-argument version). */
    inline static Node* init(Node* node, PyObject* value) {
        // Check that item is a tuple of size 2 (key-value pair)
        if (!PyTuple_Check(value) || PyTuple_Size(value) != 2) {
            PyErr_Format(
                PyExc_TypeError,
                "Expected tuple of size 2 (key, value), not: %R",
                value
            );
            return nullptr;  // propagate TypeError()
        }

        // unpack tuple and pass to 2-argument version
        PyObject* key = PyTuple_GetItem(value, 0);
        PyObject* mapped = PyTuple_GetItem(value, 1);
        return init(node, key, mapped);
    }

    /* Initialize a newly-allocated node (2-argument version). */
    inline static Node* init(Node* node, PyObject* value, PyObject* mapped) {
        node = static_cast<Node*>(NodeType::init(node, value));
        if (node == nullptr) {  // Error during decorated init()
            return nullptr;  // propagate
        }

        // compute hash
        node->hash = PyObject_Hash(value);
        if (node->hash == -1 && PyErr_Occurred()) {
            NodeType::teardown(node);  // free any resources allocated during init()
            return nullptr;  // propagate TypeError()
        }

        // store a reference to the mapped value
        Py_INCREF(mapped);
        node->mapped = mapped;

        // return initialized node
        return node;
    }

    /* Initialize a copied node. */
    inline static Node* init_copy(Node* new_node, Node* old_node) {
        new_node = static_cast<Node*>(NodeType::init_copy(new_node, old_node));
        if (new_node == nullptr) {  // Error during decrated init_copy()
            return nullptr;  // propagate
        }

        // reuse the pre-computed hash
        new_node->hash = old_node->hash;

        // store a new reference to mapped value
        Py_INCREF(old_node->mapped);
        new_node->mapped = old_node->mapped;

        // return initialized node
        return new_node;
    }

    /* Tear down a node before freeing it. */
    inline static void teardown(Node* node) {
        Py_DECREF(node->mapped);  // release mapped value
        NodeType::teardown(node);
    }

};


/* A node decorator that adds a separate mutex to each node, for use in heavily
multithreaded contexts. */
template <typename NodeType>
struct Threaded : public NodeType {
    using Node = Threaded<NodeType>;

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
        Node* result = static_cast<Node*>(NodeType::next());
        if (result != nullptr) {
            result->mutex.lock();  // lock the next node's mutex on access
        }
        return result;
    }

    // NOTE: prev() is conditionally compiled based on whether the templated node type
    // is doubly-linked (i.e. has a prev() method).  This is done using SFINAE.

    /* Access the previous node in the list. */
    inline auto prev() -> std::enable_if_t<
        std::is_same_v<decltype(std::declval<NodeType>().prev()), NodeType*>,
        Node*
    > {
        Node* result = static_cast<Node*>(NodeType::prev());
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
template <typename NodeType>
struct Counted : public NodeType {
    size_t frequency;

    /* Initialize a newly-allocated node. */
    inline static Counted<NodeType>* init(Counted<NodeType>* node, PyObject* value) {
        node = (Counted<NodeType>*)NodeType::init(node, value);
        if (node == nullptr) {  // Error during decorated init()
            return nullptr;  // propagate
        }

        // initialize frequency
        node->frequency = 1;

        // return initialized node
        return node;
    }

    /* Initialize a copied node. */
    inline static Counted<NodeType>* init_copy(
        Counted<NodeType>* new_node,
        Counted<NodeType>* old_node
    ) {
        // delegate to templated init_copy() method
        new_node = (Counted<NodeType>*)NodeType::init_copy(new_node, old_node);
        if (new_node == nullptr) {  // Error during templated init_copy()
            return nullptr;  // propagate
        }

        // copy frequency
        new_node->frequency = old_node->frequency;
        return new_node;
    }

    /* Tear down a node before freeing it. */
    inline static void teardown(Counted<NodeType>* node) {
        node->frequency = 0; // reset frequency
        NodeType::teardown(node);
    }
};


///////////////////////////
////    NODE TRAITS    ////
///////////////////////////


// TODO: replace these with inline constexpr using C++17 syntax:

// template <typename Node, typename = void>
// inline constexpr bool has_prev = false;

// template <typename Node>
// inline constexpr bool has_prev<
//     Node, std::void_t<decltype(std::declval<Node>().prev())>
// > = true;


// if constexpr (has_prev<Node>) {}
// vs
// if constexpr (has_prev<Node>::value) {}


// using C++20 traits:

/*
template <typename Node>
concept HasPrev = requires(Node node) {
    { node.prev() } -> std::convertible_to<Node*>;
};


template <typename Node>
concept HasHash = requires(Node node) {
    { node.hash } -> std::convertible_to<Py_hash_t>;
};
*/


/* A trait that detects whether the templated node type is doubly-linked (i.e.
has a `prev` pointer). */
template <typename Node>
struct has_prev {
private:
    // Helper template to detect whether Node has a `prev` field
    template <typename T>
    static std::true_type test(decltype(&T::prev)*);

    //  Overload for when `prev` is not present
    template <typename T>
    static std::false_type test(...);

public:
    static constexpr bool value = decltype(test<Node>(nullptr))::value;
};


/* A trait that detects whether the templated node type stores the hash of the
underlying value. */
template <typename Node>
struct has_hash {
private:
    // Helper template to detect whether Node has a `hash` field
    template <typename T>
    static std::true_type test(decltype(&T::hash)*);

    // Overload for when `hash` is not present
    template <typename T>
    static std::false_type test(...);

public:
    static constexpr bool value = decltype(test<Node>(nullptr))::value;
};


/* A trait that detects whether the templated node type holds a reference to a
mapped value. */
template <typename Node>
struct has_mapped {
private:
    // Helper template to detect whether Node has a `mapped` field
    template <typename T>
    static std::true_type test(decltype(&T::mapped)*);

    // Overload for when `mapped` is not present
    template <typename T>
    static std::false_type test(...);

public:
    static constexpr bool value = decltype(test<Node>(nullptr))::value;
};


/* A trait that detects whether the templated node type has a mutex for advanced
thread-safety. */
template <typename Node>
struct has_mutex {
private:
    // Helper template to detect whether Node has a `mutex` field
    template <typename T>
    static std::true_type test(decltype(&T::mutex)*);

    // Overload for when `mutex` is not present
    template <typename T>
    static std::false_type test(...);

public:
    static constexpr bool value = decltype(test<Node>(nullptr))::value;
};


// TODO: migrate code to use NodeTraits rather than has_xxx traits directly


template <typename Node>
struct NodeTraits {

    inline static constexpr bool has_prev = has_prev<Node>::value;
    inline static constexpr bool has_hash = has_hash<Node>::value;
    inline static constexpr bool has_mapped = has_mapped<Node>::value;
    inline static constexpr bool has_mutex = has_mutex<Node>::value;

    //////////////////////
    ////    SFINAE    ////
    //////////////////////

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



#endif // BERTRAND_STRUCTS_CORE_NODE_H include guard
