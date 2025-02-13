#ifndef BERTRAND_LINKED_LIST_H
#define BERTRAND_LINKED_LIST_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/static_str.h"


namespace bertrand {


namespace impl {
    struct linked_node_tag {};
    struct single_node_tag : linked_node_tag {};
    struct double_node_tag : linked_node_tag {};
    struct keyed_node_tag : linked_node_tag {};
    struct hashed_node_tag : linked_node_tag {};
}


namespace meta {

    template <typename T>
    concept linked_node = meta::inherits<T, impl::linked_node_tag>;
    template <typename T>
    concept singly_linked_node = meta::inherits<T, impl::single_node_tag>;
    template <typename T>
    concept doubly_linked_node = meta::inherits<T, impl::double_node_tag>;
    template <typename T>
    concept keyed_linked_node = meta::inherits<T, impl::keyed_node_tag>;
    template <typename T>
    concept hashed_linked_node = meta::inherits<T, impl::hashed_node_tag>;

    namespace detail {

        template <typename T>
        struct linked_hash_element {
            using type = qualify<typename std::remove_cvref_t<T>::value_type, T>;
        };
        template <keyed_linked_node T>
        struct linked_hash_element<T> {
            using type = qualify<typename std::remove_cvref_t<T>::key_type, T>;
        };

    }

    template <linked_node T>
    using linked_hash_element = detail::linked_hash_element<T>::type;

}


namespace impl {

    /* A node in a singly-linked list. */
    template <typename T>
    struct single_node : single_node_tag {
        using value_type = T;

        value_type value;
        single_node* next;

        template <std::convertible_to<value_type> U>
        single_node(U&& value, single_node* next = nullptr) :
            value(std::forward<U>(value)), next(next)
        {}

        /* Insert the node between its neighbors to form a singly-linked list.  `curr`
        must not be null, but `prev` and `next` can be. */
        static void link(
            single_node* prev,
            single_node* curr,
            single_node* next
        ) noexcept {
            if (prev) {
                prev->next = curr;
            }
            curr->next = next;
        }

        /* Unlink the node from its neighbors without breaking the list. `curr` must
        not be null, but `prev` and `next` can be. */
        static void unlink(
            single_node* prev,
            single_node* curr,
            single_node* next
        ) noexcept {
            if (prev) {
                prev->next = next;
            }
        }

        /* Break a linked list at the specified nodes, splitting it in two. */
        static void split(single_node* prev, single_node* curr) noexcept {
            if (prev) {
                prev->next = nullptr;
            }
        }

        /* Join a linked list at the specified nodes. */
        static void join(single_node* prev, single_node* curr) noexcept {
            if (prev) {
                prev->next = curr;
            }
        }

    };

    /* A node in a doubly-linked list. */
    template <typename T>
    struct double_node : double_node_tag {
        using value_type = T;

        value_type value;
        double_node* next;
        double_node* prev;

        template <std::convertible_to<value_type> U>
        double_node(
            U&& value,
            double_node* prev = nullptr,
            double_node* next = nullptr
        ) :
            value(std::forward<U>(value)),
            prev(prev),
            next(next)
        {}

        /* Insert the node between its neighbors to form a doubly-linked list. */
        static void link(
            double_node* prev,
            double_node* curr,
            double_node* next
        ) noexcept {
            if (prev) {
                prev->next = curr;
            }
            curr->prev = prev;
            curr->next = next;
            if (next) {
                next->prev = curr;
            }
        }

        /* Unlink the node from its neighbors without breaking the list. */
        static void unlink(
            double_node* prev,
            double_node* curr,
            double_node* next
        ) noexcept {
            if (prev) {
                prev->next = next;
            }
            if (next) {
                next->prev = prev;
            }
        }

        /* Break a linked list at the specified nodes, splitting it in two. */
        static void split(double_node* prev, double_node* curr) noexcept {
            if (prev) {
                prev->next = nullptr;
            }
            if (curr) {
                curr->prev = nullptr;
            }
        }

        /* Join a linked list at the specified nodes. */
        static void join(double_node* prev, double_node* curr) noexcept {
            if (prev) {
                prev->next = curr;
            }
            if (curr) {
                curr->prev = prev;
            }
        }
    };

    /* A node adaptor that adds space for a separate key to a given node type. */
    template <typename Key, meta::inherits<linked_node_tag> Node>
        requires (!meta::is_qualified<Node>)
    struct keyed_node : Node, keyed_node_tag {
        using key_type = Key;
        using value_type = Node::value_type;

        key_type key;

        template <std::convertible_to<key_type> K, typename... Args>
            requires (std::constructible_from<Node, Args...>)
        keyed_node(K&& key, Args&&... args) :
            Node(std::forward<Args>(args)...),
            key(std::forward<K>(key))
        {}
    };

    /* A node adaptor that adds space to store a hash of the underlying key/value. */
    template <meta::inherits<linked_node_tag> Node, typename Hash>
        requires (
            !meta::is_qualified<Node> &&
            !meta::is_qualified<Node> &&
            std::is_invocable_r_v<size_t, Hash, meta::linked_hash_element<Node>>
        )
    struct hashed_node : Node, hashed_node_tag {
        using hasher = Hash;

        size_t hash;

        template <typename... Args>
            requires (std::constructible_from<Node, Args...>)
        hashed_node(Args&&... args) :
            Node(std::forward<Args>(args)...),
            hash([this] {
                if constexpr (meta::inherits<Node, keyed_node_tag>) {
                    return hasher{}(this->key);
                } else {
                    return hasher{}(this->value);
                }
            }())
        {}
    };



    /* A simple, bidirectional iterator over a linked list data structure. */
    template <meta::linked_node node> requires (meta::unqualified<node>)
    struct linked_iterator {
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = node;
        using reference = node&;
        using pointer = node*;

    private:
    node* curr;

    public:
        linked_iterator(node* curr = nullptr) : curr(curr) {}

        [[nodiscard]] node& operator*() noexcept { return *curr; }
        [[nodiscard]] const node& operator*() const noexcept { return *curr; }
        [[nodiscard]] node* operator->() noexcept { return curr; }
        [[nodiscard]] const node* operator->() const noexcept { return curr; }

        linked_iterator& operator++() noexcept {
            curr = curr->next;
            return *this;
        }

        linked_iterator operator++(int) noexcept {
            linked_iterator temp = *this;
            ++(*this);
            return temp;
        }

        linked_iterator& operator--() noexcept {
            curr = curr->prev;
            return *this;
        }

        linked_iterator operator--(int) noexcept {
            linked_iterator temp = *this;
            --(*this);
            return temp;
        }

        [[nodiscard]] bool operator==(const linked_iterator& other) const noexcept {
            return curr == other.curr;
        }

        [[nodiscard]] bool operator!=(const linked_iterator& other) const noexcept {
            return curr != other.curr;
        }
    };



    /* A wrapper around a node allocator for a linked data structure, which keeps
    track of the head and tail, as well as the current occupancy and dynamic
    capacity. */
    template <meta::unqualified Node, size_t N, typename Alloc>
        requires ((N > 0 && meta::is_void<Alloc>) || meta::allocator_for<Alloc, Node>)
    struct linked_view {

        /* Indicates whether the data structure has a constant capacity (true) or
        supports reallocations (false).  If true, then the contents are guaranteed to
        have stable addresses. */
        static constexpr bool STATIC = N > 0;

    protected:
        Alloc allocator;

        /* Invoke the stored allocator to retrieve a block of nodes of a specified
        size. */
        Node* allocate(size_t n) {
            Node* data = std::allocator_traits<Alloc>::allocate(allocator, n);
            if (!data) {
                throw MemoryError();
            }
            return data;
        }

        /* Invoke the stored allocator to destroy a block of nodes of a specified
        size. */
        void deallocate(Node* data, size_t n) {
            std::allocator_traits<Alloc>::deallocate(allocator, data, n);
        }

    public:
        template <typename... Args> requires (std::constructible_from<Alloc, Args...>)
        linked_view(Args&&... args) : allocator(std::forward<Args>(args)...) {}
    };

    /* A wrapper around a node allocator for a linked list data structure, which
    keeps track of the head and tail, as well as the current occupancy and dynamic
    capacity.  Uses a free list to track deleted nodes, which will be reused when a
    new node is added according to FIFO order. */
    template <meta::unqualified Node, size_t N, typename Alloc>
        requires ((N > 0 && meta::is_void<Alloc>) || meta::allocator_for<Alloc, Node>)
    struct list_view : linked_view<Node, N, Alloc> {

        /* Indicates the minimum size for a dynamic array, to prevent thrashing.  This
        has no effect if the array is statically-sized (N > 0) */
        static constexpr size_t MIN_SIZE = 8;

    protected:
        using base = linked_view<Node, N, Alloc>;
        using base::allocate;
        using base::deallocate;

        /* A static array, which is preallocated to a fixed size and does not interact
        with the heap in any way. */
        template <size_t M>
        struct Array {
        private:
            friend list_view;
            alignas(Node) unsigned char storage[M * sizeof(Node)];  // uninitialized

        public:
            size_t capacity = M;
            size_t size = 0;
            Node* data = reinterpret_cast<Node*>(storage);
            Node* head = nullptr;
            Node* tail = nullptr;
        };

        /* A dynamic array, which can grow and shrink as needed. */
        template <>
        struct Array<0> {
            size_t capacity = 0;
            size_t size = 0;
            Node* data = nullptr;
            Node* head = nullptr;
            Node* tail = nullptr;
        };

        Array<N> array;

        /* Free list tracking removed nodes that can be recycled. */
        struct Freelist {
            Node* head = nullptr;
            Node* tail = nullptr;
        } freelist;

        /* Allocate a new array of a given size and transfer the contents. */
        void resize(size_t cap) {
            if constexpr (N) {
                throw MemoryError(
                    "cannot resize a list with fixed capacity (" +
                    static_str<>::from_int<N> + ")"
                );

            } else {
                if (cap < array.size) {
                    throw MemoryError(
                        "new capacity cannot be less than current size (" +
                        std::to_string(cap) + " < " + std::to_string(array.size) + ")"
                    );
                }
    
                // if requested capacity is zero, delete the array to save space
                if (cap == 0) {
                    if (array.data) {
                        deallocate(array.data, array.capacity);
                    }
                    array.capacity = 0;
                    array.size = 0;
                    array.data = nullptr;
                    array.head = nullptr;
                    array.tail = nullptr;
                    freelist.head = nullptr;
                    freelist.tail = nullptr;
                    return;
                }
    
                // prevent capacity from falling below threshold to avoid thrashing
                Array<N> temp {
                    .capacity = cap < MIN_SIZE ? MIN_SIZE : cap,
                    .size = 0,
                    .data = allocate(temp.capacity),
                    .head = &temp.data[0],
                    .tail = temp.head
                };
    
                // construct the first node, then continue with intermediate links
                try {
                    new (temp.head) Node(std::move(*array.head));
                    ++temp.size;
                    Node* curr = array.head->next;
                    while (curr) {
                        temp.tail->next = &temp.data[temp.size];
                        new (temp.tail->next) Node(std::move(*curr));
                        temp.tail->next->prev = temp.tail;
                        temp.tail = temp.tail->next;
                        curr = curr->next;
                        ++temp.size;
                    }
    
                // If a move constructor fails, replace the previous values
                } catch (...) {
                    Node* orig = array.head;
                    for (size_t i = 0; i < temp.size; ++i) {
                        Node& curr = temp.data[i];
                        *orig = std::move(curr);
                        curr.~Node();
                        orig = orig->next;  // move constructors don't alter original links
                    }
                    deallocate(temp.data, cap);
                    throw;
                }
            }
        }

    public:
        template <typename... Args> requires (std::constructible_from<Alloc, Args...>)
        list_view(Args&&... args) : base(std::forward<Args>(args)...) {}

        list_view(const list_view& other) : base(other) {
            if (other.empty()) {
                return;
            }

            // otherwise, only allocate enough space for the other list's contents
            if constexpr (N == 0) {
                array.capacity = other.size() < MIN_SIZE ? MIN_SIZE : other.size();
                array.data = allocate(array.capacity);
            }
            array.head = &array.data[0];
            array.tail = array.head;

            // construct the first node, then continue with intermediate links
            try {
                new (array.head) Node(*other.array.head);
                ++array.size;
                Node* curr = other.array.head->next;
                while (curr) {
                    array.tail->next = &array.data[array.size];
                    new (array.tail->next) Node(*curr);
                    array.tail->next->prev = array.tail;
                    array.tail = array.tail->next;
                    curr = curr->next;
                    ++array.size;
                }

            // If a copy constructor fails, destroy nodes that have been added
            } catch (...) {
                for (size_t i = 0; i < array.size; ++i) {
                    array.data[i].~Node();
                }
                if constexpr (N == 0) {
                    deallocate(array.data, array.capacity);
                }
                throw;
            }
        }

        list_view(list_view&& other) : base(std::move(other)) {
            if constexpr (N) {
                if (other.empty()) {
                    return;
                }
                array.head = &array.data[0];
                array.tail = array.head;

                // construct the first node, then continue with intermediate links
                try {
                    new (array.head) Node(*std::move(other.array.head));
                    ++array.size;
                    Node* curr = other.array.head->next;
                    while (curr) {
                        array.tail->next = &array.data[array.size];
                        new (array.tail->next) Node(*std::move(curr));
                        array.tail->next->prev = array.tail;
                        array.tail = array.tail->next;
                        curr = curr->next;
                        ++array.size;
                    }

                // If a move constructor fails, replace the previous values
                } catch (...) {
                    Node* orig = array.head;
                    for (size_t i = 0; i < array.size; ++i) {
                        Node& curr = array.data[i];
                        *orig = std::move(curr);
                        curr.~Node();
                        orig = orig->next;  // move constructors don't alter original links
                    }
                    throw;
                }
            } else {
                array = other.array;
                freelist = other.freelist;
                other.array.capacity = 0;
                other.array.data = nullptr;
                other.array.size = 0;
                other.array.head = nullptr;
                other.array.tail = nullptr;
                other.freelist.head = nullptr;
                other.freelist.tail = nullptr;
            }
        }

        list_view& operator=(const list_view& other) {
            if (this != &other) {
                // delete current contents
                if (array.data) {
                    Node* curr = array.head;
                    while (curr) {
                        Node* next = curr->next;
                        curr->~Node();
                        curr = next;
                    }
                    if constexpr (N == 0) {
                        deallocate(array.data, array.capacity);
                    }
                }

                base::allocator = other.allocator;
                if constexpr (N == 0) {
                    array.capacity = 0;
                    array.data = nullptr;
                }
                array.size = 0;
                array.head = nullptr;
                array.tail = nullptr;
                freelist.head = nullptr;
                freelist.tail = nullptr;

                // avoid allocation if other list is empty
                if (!other.empty()) {
                    if constexpr (N == 0) {
                        array.capacity = other.size() < MIN_SIZE ? MIN_SIZE : other.size();
                        array.data = allocate(array.capacity);
                    }
                    array.head = &array.data[0];
                    array.tail = array.head;

                    // construct the first node, then continue with intermediate links
                    try {
                        new (array.head) Node(*other.array.head);
                        ++array.size;
                        Node* curr = other.array.head->next;
                        while (curr) {
                            array.tail->next = &array.data[array.size];
                            new (array.tail->next) Node(*curr);
                            array.tail->next->prev = array.tail;
                            array.tail = array.tail->next;
                            curr = curr->next;
                            ++array.size;
                        }

                    // If a copy constructor fails, destroy nodes that have been added
                    } catch (...) {
                        for (size_t i = 0; i < array.size; ++i) {
                            array.data[i].~Node();
                        }
                        if constexpr (N == 0) {
                            deallocate(array.data, array.capacity);
                            array.capacity = 0;
                            array.data = nullptr;
                        }
                        array.size = 0;
                        array.head = nullptr;
                        array.tail = nullptr;
                        throw;
                    }
                }
            }
            return *this;
        }

        list_view& operator=(list_view&& other) {
            if (this != &other) {
                /// TODO: same as move constructor for static/dynamic cases

                if (array.data) {
                    Node* curr = array.head;
                    while (curr) {
                        Node* next = curr->next;
                        curr->~Node();
                        curr = next;
                    }
                    deallocate(array.data, array.capacity);
                }
                base::allocator = std::move(other.allocator);
                array = other.array;
                freelist = other.freelist;
                other.array.data = nullptr;
                other.array.head = nullptr;
                other.array.tail = nullptr;
                other.array.size = 0;
                other.array.capacity = 0;
                other.freelist.head = nullptr;
                other.freelist.tail = nullptr;
            }
            return *this;
        }

        ~list_view() noexcept {
            if (array.size) {
                Node* curr = array.head;
                while (curr) {
                    Node* next = curr->next;
                    curr->~Node();
                    curr = next;
                }
                if constexpr (N == 0) {
                    deallocate(array.data, array.capacity);
                }
            }
        }

        /* A reference to the head of the list.  May be null if the list is empty. */
        [[nodiscard]] Node* head() const noexcept { return array.head; }

        /* A reference to the tail of the list.  May be null if the list is empty. */
        [[nodiscard]] Node* tail() const noexcept { return array.tail; }

        /* The number of initialized nodes in the array. */
        [[nodiscard]] size_t size() const noexcept { return array.size; }

        /* True if the list has zero size.  False otherwise. */
        [[nodiscard]] bool empty() const noexcept { return !array.size; }

        /* The total number of nodes the array can store before resizing. */
        [[nodiscard]] size_t capacity() const noexcept { return array.capacity; }

        /* Estimate the overall memory usage of the list in bytes. */
        [[nodiscard]] size_t memory_usage() const noexcept {
            return sizeof(list_view) + array.capacity * sizeof(Node);
        }

        /* Initialize a new node for the list.  The result has null `prev` and `next`
        pointers, and is initially disconnected from all other nodes.  It is included
        in `size()`, however. */
        template <typename... Args> requires (std::constructible_from<Node, Args...>)
        [[nodiscard]] Node* create(Args&&... args) {
            // check free list for recycled nodes
            if (freelist.head) {
                Node* node = freelist.head;
                Node* next = node->next;
                new (node) Node(std::forward<Args>(args)...);
                ++array.size;
                freelist.head = next;
                if (!next) {
                    freelist.tail = nullptr;
                }
                return node;
            }

            // check if we need to grow the array
            if (array.size == array.capacity) {
                if constexpr (N) {
                    throw MemoryError(
                        "cannot resize a list with fixed capacity (" +
                        static_str<>::from_int<N> + ")"
                    );
                } else {
                    // growth factor of 1.5 minimizes fragmentation
                    resize(array.capacity + array.capacity / 2);
                }
            }

            // initialize from end of allocated section
            Node* node = array.data + array.size;
            new (node) Node(std::forward<Args>(args)...);
            ++array.size;
            return node;
        }

        /* Destroy a node from the list, returning it to the dynamic array.  Note that
        the node should be disconnected from all other nodes before calling this
        method, and the node must have been allocated from this view. */
        void recycle(Node* node) {
            if constexpr (DEBUG) {
                if (node < array.data || node >= array.data + array.capacity) {
                    throw MemoryError("node was not allocated from this view");
                }
                if (node->prev) {
                    throw MemoryError(
                        "node->prev = " +
                        std::to_string(reinterpret_cast<std::uintptr_t>(node->prev)) +
                        " (should be null)"
                    );
                }
                if (node->next) {
                    throw MemoryError(
                        "node->next = " +
                        std::to_string(reinterpret_cast<std::uintptr_t>(node->next)) +
                        " (should be null)"
                    );
                }
            }
            node->~Node();
            --array.size;
            if (freelist.head) {
                freelist.tail->next = node;
                freelist.tail = node;
            } else {
                freelist.head = node;
                freelist.tail = node;
            }
        }

        /* Remove all nodes from the list, resetting the size to zero, but leaving the
        capacity unchanged. */
        void clear() {
            Node* curr = array.head;
            while (curr) {
                Node* next = curr->next;
                curr->~Node();
                curr = next;
            }
            array.size = 0;
            array.head = nullptr;
            array.tail = nullptr;
            freelist.head = nullptr;
            freelist.tail = nullptr;
        }

        /* Resize the allocator to store at least the given number of nodes. */
        void reserve(size_t n) {
            if constexpr (N) {
                throw MemoryError(
                    "cannot resize a list with fixed capacity (" +
                    static_str<>::from_int<N> + ")"
                );
            } else {
                if (n > array.capacity) {
                    resize(n);
                }
            }
        }

        /* Rearrange the nodes in memory to reflect their current list order, without
        changing the capacity.  This is done automatically whenever the underlying
        array grows or shrinks, but may be triggered manually as an optimization. */
        void defragment() {
            if constexpr (N) {
                /// TODO: I might be able to defragment a static array by moving values
                /// into a temporary array.  It's just complicated.
                throw MemoryError(
                    "cannot defragment a list with fixed capacity (" +
                    static_str<>::from_int<N> + ")"
                );
            } else {
                if (array.capacity) {
                    resize(array.capacity);
                }
            }
        }

        /* Shrink the capacity to the current size.  If the list is empty, then the
        underlying array will be deleted and the capacity set to zero.  Otherwise, if
        there are fewer than `MIN_SIZE` nodes, the capacity is set to `MIN_SIZE`
        instead. */
        void shrink() {
            if constexpr (N) {
                throw MemoryError(
                    "cannot resize a list with fixed capacity (" +
                    static_str<>::from_int<N> + ")"
                );
            } else {
                resize(array.size);
            }
        }
    };



    /* A wrapper around a node allocator for a linked set or map. */
    template <typename Node>
    struct hash_view {

    };



    /// TODO: allocator views.  These should take a `std::allocator` type as a template
    /// parameter.  This is really where most of the engineering effort would be.

}



template <typename T, size_t N = 0, typename Less = void, typename Alloc = std::allocator<T>>
    requires (
        /// TODO: T can be a reference?
        meta::allocator_for<Alloc, T>
    )
struct linked_list {
    using value_type = T;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;

protected:

    /* Node type used to store each value.  Note that constructors/assignment operators
    do not modify links between nodes, which must be manually assigned. */
    struct Node {
        value_type value;
        Node* prev = nullptr;
        Node* next = nullptr;

        template <typename... Args>
            requires (std::constructible_from<value_type, Args...>)
        Node(Args&&... args) : value(std::forward<Args>(args)...) {}
        Node(const Node& other) : value(other.value) {}
        Node(Node&& other) : value(std::move(other.value)) {}
        Node& operator=(const Node& other) {
            if (this != &other) {
                value = other.value;
            }
            return *this;
        }
        Node& operator=(Node&& other) {
            if (this != &other) {
                value = std::move(other.value);
            }
            return *this;
        }
    };

    using Allocator = std::allocator_traits<Alloc>::template rebind_alloc<Node>;

public:


};


template <typename T>
struct linked_set {


protected:


public:

};


template <typename K, typename V>
struct linked_map {

};


/// TODO: specific LRU variations



}  // namespace bertrand


#endif  // BERTRAND_LINKED_LIST_H
