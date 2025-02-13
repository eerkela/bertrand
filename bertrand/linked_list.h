#ifndef BERTRAND_LINKED_LIST_H
#define BERTRAND_LINKED_LIST_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/static_str.h"


namespace bertrand {


namespace impl {
    struct linked_node_tag {};
    struct list_view_tag {};
    struct hash_view_tag {};
}


namespace meta {

    template <typename T>
    concept linked_node = meta::inherits<T, impl::linked_node_tag>;

    template <typename T>
    concept hashed_node = linked_node<T> && requires(T node) {
        { node.hash } -> std::convertible_to<size_t>;
    };

}


namespace impl {

    /* Node type used to store each value in a linked data structure.  Note that
    constructors/assignment operators do not modify links between nodes, which must be
    manually assigned. */
    template <typename T>
    struct linked_node : linked_node_tag {
        using value_type = T;

        value_type value;
        linked_node* prev = nullptr;
        linked_node* next = nullptr;

        template <typename... Args> requires (std::constructible_from<value_type, Args...>)
        linked_node(Args&&... args) : value(std::forward<Args>(args)...) {}
        linked_node(const linked_node& other) : value(other.value) {}
        linked_node(linked_node&& other) : value(std::move(other.value)) {}
        linked_node& operator=(const linked_node& other) {
            if (this != &other) {
                value = other.value;
            }
            return *this;
        }
        linked_node& operator=(linked_node&& other) {
            if (this != &other) {
                value = std::move(other.value);
            }
            return *this;
        }
    };

    /* A modified node type that automatically computes a hash for the underlying value
    using a customizable hash function.  Note that constructors/assignment operators do
    not modify links between nodes, which must be manually assigned. */
    template <typename T, typename Hash>
        requires (
            std::is_default_constructible_v<Hash> &&
            std::is_invocable_r_v<size_t, Hash, T>
        )
    struct hashed_node : linked_node_tag {
        using value_type = T;
        using hasher = Hash;

        value_type value;
        size_t hash;
        hashed_node* prev = nullptr;
        hashed_node* next = nullptr;

        template <typename... Args>
            requires (std::constructible_from<value_type, Args...>)
        hashed_node(Args&&... args) :
            value(std::forward<Args>(args)...),
            hash(hasher{}(this->value))
        {}

        hashed_node(const hashed_node& other) : value(other.value) {}
        hashed_node(hashed_node&& other) : value(std::move(other.value)) {}
        hashed_node& operator=(const hashed_node& other) {
            if (this != &other) {
                value = other.value;
            }
            return *this;
        }
        hashed_node& operator=(hashed_node&& other) {
            if (this != &other) {
                value = std::move(other.value);
            }
            return *this;
        }
    };

    /* A simple, bidirectional iterator over a linked list data structure. */
    template <meta::linked_node Node> requires (!std::is_reference_v<Node>)
    struct linked_node_iterator {
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = Node;
        using reference = Node&;
        using pointer = Node*;

    private:
        Node* curr;

    public:
        linked_node_iterator(Node* curr = nullptr) noexcept : curr(curr) {}

        [[nodiscard]] Node& operator*() noexcept { return *curr; }
        [[nodiscard]] const Node& operator*() const noexcept { return *curr; }
        [[nodiscard]] Node* operator->() noexcept { return curr; }
        [[nodiscard]] const Node* operator->() const noexcept { return curr; }

        linked_node_iterator& operator++() noexcept {
            curr = curr->next;
            return *this;
        }

        [[nodiscard]] linked_node_iterator operator++(int) noexcept {
            linked_node_iterator temp = *this;
            ++(*this);
            return temp;
        }

        linked_node_iterator& operator--() noexcept {
            curr = curr->prev;
            return *this;
        }

        [[nodiscard]] linked_node_iterator operator--(int) noexcept {
            linked_node_iterator temp = *this;
            --(*this);
            return temp;
        }

        [[nodiscard]] bool operator==(const linked_node_iterator& other) const noexcept {
            return curr == other.curr;
        }

        [[nodiscard]] bool operator!=(const linked_node_iterator& other) const noexcept {
            return curr != other.curr;
        }
    };

    /* A reversed version of the above iterator.  Inheritance allows a forward iterator
    to be converted into a reverse iterator and vice versa. */
    template <meta::linked_node Node> requires (!std::is_reference_v<Node>)
    struct reverse_linked_node_iterator : linked_node_iterator<Node> {
    private:
        using base = linked_node_iterator<Node>;

    public:
        using base::base;

        reverse_linked_node_iterator& operator++() noexcept {
            base::operator--();
            return *this;
        }

        [[nodiscard]] reverse_linked_node_iterator operator++(int) noexcept {
            reverse_linked_node_iterator temp = *this;
            ++(*this);
            return temp;
        }

        reverse_linked_node_iterator& operator--() noexcept {
            base::operator++();
            return *this;
        }

        [[nodiscard]] reverse_linked_node_iterator operator--(int) noexcept {
            reverse_linked_node_iterator temp = *this;
            --(*this);
            return temp;
        }
    };

    /// TODO: set propagate_on_container_copy_assignment, etc. for better stdlib
    /// compatibility/performance

    /* A wrapper around a node allocator for a linked list data structure, which
    keeps track of the head, tail, size, and capacity of the underlying array, along
    with several helper methods for low-level memory management.

    The memory is laid out as a contiguous array of nodes similar to a `std::vector`,
    but with added `prev` and `next` pointers between nodes.  The array block is
    allocated using the provided allocator, and is filled up from left to right,
    maintaining contiguous order.  If a node is removed from the list, it is added to a
    singly-linked free list composed of the `next` pointers of the removed nodes, which
    is checked when allocating new nodes.  This allows the contiguous condition to be
    violated, fragmenting the array and allowing O(1) reordering.  In order to maximize
    cache locality, the array is automatically defragmented (returned to contiguous
    order) whenever it is copied, emptied, or resized.  Manual defragments can also be
    done via the `defragment()` method.

    If `N` is greater than zero, then the array will be allocated on the stack with a
    fixed size of `N` elements.  Such an array cannot be resized and will throw an
    exception if filled past capacity, or if the `reserve()`, `defragment()`, or
    `shrink()` methods are called.  All elements in the array are guaranteed to have
    stable addresses for their full duration within the list.  Defragmentation will
    only occur on copy or upon removing the last element of the list. */
    template <meta::unqualified Node, size_t N, meta::unqualified Alloc>
        requires (meta::linked_node<Node> && meta::allocator_for<Alloc, Node>)
    struct list_view : list_view_tag {
        using allocator_type = Alloc;
        using size_type = size_t;
        using difference_type = std::ptrdiff_t;
        using value_type = Node;
        using reference = Node&;
        using const_reference = const Node&;
        using pointer = Node*;
        using const_pointer = const Node*;
        using iterator = linked_node_iterator<Node>;
        using const_iterator = linked_node_iterator<const Node>;
        using reverse_iterator = reverse_linked_node_iterator<Node>;
        using const_reverse_iterator = reverse_linked_node_iterator<const Node>;

        /* Indicates whether the data structure has a fixed capacity (true) or supports
        reallocations (false).  If true, then the contents are guaranteed to retain
        stable addresses. */
        static constexpr bool STATIC = N > 0;

        /* The minimum size for a dynamic array, to prevent thrashing.  This has no
        effect if the array has a fixed capacity (N > 0) */
        static constexpr size_t MIN_SIZE = 8;

    protected:
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

        Alloc allocator;
        Node* freelist = nullptr;
        Array<N> array;

        Node* allocate(size_t n) {
            Node* data = std::allocator_traits<Alloc>::allocate(allocator, n);
            if (!data) {
                throw MemoryError();
            }
            return data;
        }

        void deallocate(Node* data, size_t n) {
            std::allocator_traits<Alloc>::deallocate(allocator, data, n);
        }

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
                    freelist = nullptr;
                    array.capacity = 0;
                    array.size = 0;
                    array.data = nullptr;
                    array.head = nullptr;
                    array.tail = nullptr;
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

        size_t normalize_index(ssize_t i) {
            if (i < 0) {
                i += array.size;
            }
            if (i < 0 || i >= array.size) {
                throw IndexError(std::to_string(i));
            }
            return static_cast<size_t>(i);
        }

    public:
        template <typename... Args> requires (std::constructible_from<Alloc, Args...>)
        list_view(Args&&... args) : allocator(std::forward<Args>(args)...) {}

        list_view(const list_view& other) : allocator(other.allocator) {
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

        list_view(list_view&& other) : allocator(std::move(other.allocator)) {
            // if the array has fixed size, then we need to move each element manually
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
                    Node* orig = other.array.head;
                    for (size_t i = 0; i < array.size; ++i) {
                        Node& curr = array.data[i];
                        *orig = std::move(curr);
                        curr.~Node();
                        orig = orig->next;  // move constructors don't alter original links
                    }
                    throw;
                }

            // otherwise, we can just transfer ownership
            } else {
                freelist = other.freelist;
                array = other.array;
                other.freelist = nullptr;
                other.array.capacity = 0;
                other.array.size = 0;
                other.array.data = nullptr;
                other.array.head = nullptr;
                other.array.tail = nullptr;
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

                allocator = other.allocator;
                if constexpr (N == 0) {
                    array.capacity = 0;
                    array.data = nullptr;
                }
                freelist = nullptr;
                array.size = 0;
                array.head = nullptr;
                array.tail = nullptr;

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

                allocator = std::move(other.allocator);

                // if the array has fixed size, then we need to move each element manually
                if constexpr (N) {
                    freelist = nullptr;
                    array.size = 0;
                    if (other.empty()) {
                        array.head = nullptr;
                        array.tail = nullptr;
                        return *this;
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
                        Node* orig = other.array.head;
                        for (size_t i = 0; i < array.size; ++i) {
                            Node& curr = array.data[i];
                            *orig = std::move(curr);
                            curr.~Node();
                            orig = orig->next;  // move constructors don't alter original links
                        }
                        throw;
                    }

                // otherwise, we can just transfer ownership
                } else {
                    freelist = other.freelist;
                    array = other.array;
                    other.array.capacity = 0;
                    other.array.size = 0;
                    other.array.data = nullptr;
                    other.array.head = nullptr;
                    other.array.tail = nullptr;
                    other.freelist = nullptr;
                }
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

        /* True if the list has nonzero size.  False otherwise. */
        [[nodiscard]] explicit operator bool() const noexcept { return !empty(); }

        /* The total number of nodes the array can store before resizing. */
        [[nodiscard]] size_t capacity() const noexcept { return array.capacity; }

        /* Estimate the overall memory usage of the list in bytes. */
        [[nodiscard]] size_t memory_usage() const noexcept {
            return sizeof(list_view) + array.capacity * sizeof(Node);
        }

        /* Initialize a new node for the list.  The result has null `prev` and `next`
        pointers, and is initially disconnected from all other nodes, though it is
        included in `size()`.  This can cause the array to grow if it does not have a
        fixed capacity, otherwise this method will throw a `MemoryError`. */
        template <typename... Args> requires (std::constructible_from<Node, Args...>)
        [[nodiscard]] Node* create(Args&&... args) {
            // check free list for recycled nodes
            if (freelist) {
                Node* node = freelist;
                Node* next = freelist->next;
                new (node) Node(std::forward<Args>(args)...);
                ++array.size;
                freelist = next;
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

        /* Destroy a node from the list, inserting it into the free list.  Note that
        the node should be disconnected from all other nodes before calling this
        method, and the node must have been allocated from this view.  Also defragments
        the array if this is the last node in the list. */
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

            // if this was the last node, reset the freelist to naturally defragment
            // the array.  Otherwise, append to freelist.
            if (!array.size) {
                freelist = nullptr;
            } else {
                node->next = freelist;
                freelist = node;
            }
        }

        /* Remove all nodes from the list, resetting the size to zero, but leaving the
        capacity unchanged.  Also defragments the array. */
        void clear() {
            Node* curr = array.head;
            while (curr) {
                Node* next = curr->next;
                curr->~Node();
                curr = next;
            }
            freelist = nullptr;
            array.size = 0;
            array.head = nullptr;
            array.tail = nullptr;
        }

        /* Resize the allocator to store at least the given number of nodes.  Throws
        a `MemoryError` if used on a list with fixed capacity. */
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
        array grows or shrinks, but may be triggered manually as an optimization.
        Throws a `MemoryError` if used on a list with fixed capacity. */
        void defragment() {
            if constexpr (N) {
                throw MemoryError(
                    "cannot resize a list with fixed capacity (" +
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
        instead.  Throws a `MemoryError` if used on a list with fixed capacity. */
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

        [[nodiscard]] iterator begin() noexcept { return {array.head}; }
        [[nodiscard]] const_iterator begin() const noexcept { return {array.head}; }
        [[nodiscard]] const_iterator cbegin() const noexcept { return {array.head}; }
        [[nodiscard]] iterator end() noexcept { return {nullptr}; }
        [[nodiscard]] const_iterator end() const noexcept { return {nullptr}; }
        [[nodiscard]] const_iterator cend() const noexcept { return {nullptr}; }
        [[nodiscard]] reverse_iterator rbegin() noexcept { return {array.tail}; }
        [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return {array.tail}; }
        [[nodiscard]] const_reverse_iterator crbegin() const noexcept { return {array.tail}; }
        [[nodiscard]] reverse_iterator rend() noexcept { return {nullptr}; }
        [[nodiscard]] const_reverse_iterator rend() const noexcept { return {nullptr}; }
        [[nodiscard]] const_reverse_iterator crend() const noexcept { return {nullptr}; }

        /* Index into the list, returning an iterator to the specified element.  Allows
        Python-style negative indexing, and throws an `IndexError` if it is out of
        bounds after normalization.  Has a time compexity of O(n/2) following the links
        between each node starting from the nearest edge. */
        [[nodiscard]] iterator operator[](ssize_t i) {
            size_t idx = normalize_index(i);

            // if the index is closer to the head of the list, start there.
            if (idx <= array.size / 2) {
                iterator it {array.head};
                for (size_t j = 0; j++ < idx;) {
                    ++it;
                }
                return it;
            }

            // otherwise, start at the tail of the list
            iterator it = {array.tail};
            ++idx;
            for (size_t j = array.size; j-- > idx;) {
                --it;
            }
            return it;
        }

        /* Index into the list, returning an iterator to the specified element.  Allows
        Python-style negative indexing, and throws an `IndexError` if it is out of
        bounds after normalization.  Has a time compexity of O(n/2) following the links
        between each node starting from the nearest edge. */
        [[nodiscard]] const_iterator operator[](ssize_t i) const {
            size_t idx = normalize_index(i);

            // if the index is closer to the head of the list, start there.
            if (idx <= array.size / 2) {
                const_iterator it {array.head};
                for (size_t j = 0; j++ < idx;) {
                    ++it;
                }
                return it;
            }

            // otherwise, start at the tail of the list
            const_iterator it = {array.tail};
            ++idx;
            for (size_t j = array.size; j-- > idx;) {
                --it;
            }
            return it;
        }
    };

    /* A wrapper around a node allocator for a linked set or map. */
    template <meta::unqualified Node, size_t N, meta::unqualified Alloc>
        requires (meta::hashed_node<Node> && meta::allocator_for<Alloc, Node>)
    struct hash_view : hash_view_tag {
        /// TODO: reimplement the hopscotch hashing algorithm

    };

}


namespace meta {

    template <typename T>
    concept list_view = meta::inherits<T, impl::list_view_tag>;

    template <typename T>
    concept hash_view = meta::inherits<T, impl::hash_view_tag>;

}


/// TODO: maybe all the algorithms can be placed here under the impl:: namespace?
/// They'll take up a fair amount of space, but that would mean the entire linked
/// data structure ecosystem would be stored in just a single file here, which makes
/// it easier to test with syntax highlighting.



/// TODO: Less{} converts a list or set into a binary search tree (red-black tree)
/// that maintains sorted order, and is contiguous in memory.  Neither case interferes
/// with the view allocators I wrote above, since they only concern the links between
/// nodes.  BSTs are almost always preferable to skip lists, and I can do it with
/// relatively little fuss, just by modifying the node type to add a `parent` pointer.
/// That doesn't require the elements to be strictly unique, so a linked_list that
/// uses a binary search tree for ordering will remain contiguous in memory, and would
/// be usable as a list with O(1) lookups.  On the set/map side, you would be able to
/// use custom ordering conditions (like MFU/LFU) with guaranteed ordering at all times,
/// and reinserts would be O(log n) instead of O(n).  In-order traversals would also
/// benefit from contiguity at all times.



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
    using Node = impl::linked_node<T>;
    using Allocator = std::allocator_traits<Alloc>::template rebind_alloc<Node>;

    impl::list_view<Node, N, Allocator> view;

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
