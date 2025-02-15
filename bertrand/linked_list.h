#ifndef BERTRAND_LINKED_LIST_H
#define BERTRAND_LINKED_LIST_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/static_str.h"


namespace bertrand {


namespace impl::linked {

    struct node_tag {};
    struct view_tag {};

    constexpr uintptr_t NODE_ALIGNMENT = 8;

}  // namespace impl::linked


namespace meta {

    template <typename T>
    concept linked_node = meta::inherits<T, impl::linked::node_tag> && requires(T node) {
        typename std::remove_cvref_t<T>::value_type;
        { node.prev } -> std::convertible_to<std::add_pointer_t<T>>;
        { node.next } -> std::convertible_to<std::add_pointer_t<T>>;
        { node.value } -> std::convertible_to<typename std::remove_cvref_t<T>::value_type>;
    };

    template <typename T>
    concept bst_node = linked_node<T> && requires(T node) {
        typename std::remove_cvref_t<T>::less_type;
        !meta::is_void<typename std::remove_cvref_t<T>::less_type>;
        { node.prev_thread } -> std::convertible_to<bool>;
        { node.next_thread } -> std::convertible_to<bool>;
        { node.red } -> std::convertible_to<bool>;
    };

    template <typename T>
    concept hash_node = linked_node<T> && requires(T node) {
        typename std::remove_cvref_t<T>::hash_type;
        !meta::is_void<typename std::remove_cvref_t<T>::hash_type>;
        { node.hash } -> std::convertible_to<size_t>;
    };

    template <typename T>
    concept linked_view = meta::inherits<T, impl::linked::view_tag>;

    template <linked_view T>
    using node_type = std::remove_cvref_t<T>::value_type;

    template <typename T>
    concept bst_view = linked_view<T> && bst_node<node_type<T>>;

    template <typename T>
    concept hash_view = linked_view<T> && hash_node<node_type<T>>;

}  // namespace meta


namespace impl::linked {

    /// NOTE: node constructors/assignment operators do not modify links between nodes,
    /// which are not safe to copy.  Instead, they initialize to null, and must be
    /// manually assigned by the user to prevent dangling pointers.

    /// NOTE: Binary search tree (BST) nodes are implemented according to a threaded,
    /// top-down red-black design, which encodes the extra threading and color bits
    /// directly into the `prev` and `next` pointers by forcing 8-byte alignment at
    /// all times.  This means that in-order traversals can be done without a `parent`
    /// pointer, recursive stack, or auxiliary data structure, just by following the
    /// pointers like an ordinary doubly-linked list.  This both minimizes memory usage
    /// and allows many of the same algorithms to be used interchangeably for both data
    /// structures, without any extra work.

    /* Node type for hashed BST nodes. */
    template <typename T, typename Hash = void, typename Less = void>
        requires ((meta::is_void<Hash> || (
            std::is_default_constructible_v<Hash> &&
            std::is_invocable_r_v<size_t, Hash, const T&>
        )) && (meta::is_void<Less> || (
            std::is_default_constructible_v<Less> &&
            std::is_invocable_r_v<bool, Less, const T&, const T&>
        )))
    struct alignas(NODE_ALIGNMENT) node : node_tag {
    private:

        template <typename U>
        struct _unwrap_node { using type = U; };
        template <meta::linked_node U>
        struct _unwrap_node<U> { using type = meta::node_type<U>; };
        template <typename U>
        using unwrap_node = _unwrap_node<U>::type;

        /* BSTs use a tagged pointer to store the `prev` and `next` pointers, which
        encode the thread bits and red/black color bit into the pointer itself, to
        avoid auxiliary data structures and padding. */
        enum Flags : uintptr_t {
            THREAD          = 0b1,  // if set, pointer is not a real child of this node
            RED             = 0b10,  // colors this node during red-black balancing
            MASK            = THREAD | RED,
        };

        uintptr_t m_prev = 0;
        uintptr_t m_next = 0;

    public:
        using value_type = T;
        using hash_type = Hash;
        using less_type = Less;

        /* The `prev` pointer can be read from and assigned to just like normal. */
        __declspec(property(get = _get_prev, put = _set_prev))
        node* prev;
        [[nodiscard, gnu::always_inline]] node* _get_prev() const noexcept {
            return reinterpret_cast<node*>(m_prev & ~MASK);
        }
        [[gnu::always_inline]] void _set_prev(node* p) noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (reinterpret_cast<uintptr_t>(p) & MASK) {
                    throw MemoryError(
                        "node->prev cannot be assigned to a pointer that is not "
                        "8-byte aligned: " + std::to_string(
                            reinterpret_cast<uintptr_t>(p)
                        )
                    );
                }
            }
            m_prev &= MASK;
            m_prev |= reinterpret_cast<uintptr_t>(p);
        }

        /* The `next` pointer can be read from and assigned to just like normal. */
        __declspec(property(get = _get_next, put = _set_next))
        node* next;
        [[nodiscard, gnu::always_inline]] node* _get_next() const noexcept {
            return reinterpret_cast<node*>(m_next & ~MASK);
        }
        [[gnu::always_inline]] void _set_next(node* p) noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (reinterpret_cast<uintptr_t>(p) & MASK) {
                    throw MemoryError(
                        "node->next cannot be assigned to a pointer that is not "
                        "8-byte aligned: " + std::to_string(
                            reinterpret_cast<uintptr_t>(p)
                        )
                    );
                }
            }
            m_next &= MASK;
            m_next |= reinterpret_cast<uintptr_t>(p);
        }

        /* `node->prev_thread` uses an internal tag bit to tell whether the `prev`
        pointer is a BST thread, and not a true child of this node. */
        __declspec(property(get = _get_prev_thread, put = _set_prev_thread))
        bool prev_thread;
        [[nodiscard, gnu::always_inline]] bool _get_prev_thread() const noexcept {
            return m_prev & Flags::THREAD;
        }
        [[gnu::always_inline]] void _set_prev_thread(bool b) noexcept {
            m_prev &= ~Flags::THREAD;
            m_prev |= Flags::THREAD * b;
        }

        /* `node->next_thread` uses an internal tag bit to tell whether the `next`
        pointer is a BST thread, and not a true child of this node. */
        __declspec(property(get = _get_next_thread, put = _set_next_thread))
        bool next_thread;
        [[nodiscard, gnu::always_inline]] bool _get_next_thread() const noexcept {
            return m_next & Flags::THREAD;
        }
        [[gnu::always_inline]] void _set_next_thread(bool b) noexcept {
            m_next &= ~Flags::THREAD;
            m_next |= Flags::THREAD * b;
        }

        /* `node->red` returns the state of an extra color bit stored in the `prev`
        pointer. */
        __declspec(property(get = _get_red, put = _set_red))
        bool red;
        [[nodiscard, gnu::always_inline]] bool _get_red() const noexcept {
            return m_prev & Flags::RED;
        }
        [[gnu::always_inline]] void _set_red(bool b) noexcept {
            m_prev &= ~Flags::RED;
            m_prev |= Flags::RED * b;
        }

        value_type value;
        size_t hash;

        template <typename... Args> requires (std::constructible_from<value_type, Args...>)
        node(Args&&... args) noexcept(
            noexcept(value_type(std::forward<Args>(args)...)) &&
            noexcept(hash_type{}(value))
        ) :
            value(std::forward<Args>(args)...),
            hash(hash_type{}(value))
        {}

        node(const node& other) noexcept(noexcept(value_type(other.value))) :
            value(other.value),
            hash(other.hash)
        {}

        node(node&& other) noexcept(noexcept(value_type(std::move(other.value)))) :
            value(std::move(other.value)),
            hash(other.hash)
        {}

        node& operator=(const node& other) noexcept(noexcept(value = other.value)) {
            if (this != &other) {
                value = other.value;
                hash = other.hash;
            }
            return *this;
        }

        node& operator=(node&& other) noexcept(noexcept(value = std::move(other.value))) {
            if (this != &other) {
                value = std::move(other.value);
                hash = other.hash;
            }
            return *this;
        }

        template <typename U>
            requires (
                std::is_invocable_r_v<
                    bool,
                    less_type,
                    const value_type&,
                    const unwrap_node<U>&
                >
            )
        [[nodiscard]] friend bool operator<(const node& lhs, const U& rhs) noexcept(
            std::is_nothrow_invocable_r_v<
                bool,
                less_type,
                const value_type&,
                const unwrap_node<U>&
            >
        ) {
            if constexpr (meta::linked_node<U>) {
                return less_type{}(lhs.value, rhs.value);
            } else {
                return less_type{}(lhs.value, rhs);
            }
        }

        template <typename U>
            requires (std::is_invocable_r_v<
                bool,
                less_type,
                const unwrap_node<U>&,
                const value_type&
            >)
        [[nodiscard]] friend bool operator<(const U& rhs, const node& lhs) noexcept(
            std::is_nothrow_invocable_r_v<
                bool,
                less_type,
                const unwrap_node<U>&,
                const value_type&
            >
        ) {
            if constexpr (meta::linked_node<U>) {
                return less_type{}(rhs.value, lhs.value);
            } else {
                return less_type{}(rhs, lhs.value);
            }
        }
    };

    /* Node type for non-hashed BST nodes. */
    template <typename T, typename Less>
    struct alignas(NODE_ALIGNMENT) node<T, void, Less> : node_tag {
        private:

        template <typename U>
        struct _unwrap_node { using type = U; };
        template <meta::linked_node U>
        struct _unwrap_node<U> { using type = meta::node_type<U>; };
        template <typename U>
        using unwrap_node = _unwrap_node<U>::type;

        /* BSTs use a tagged pointer to store the `prev` and `next` pointers, which
        encode the thread bits and red/black color bit into the pointer itself, to
        avoid auxiliary data structures and padding. */
        enum Flags : uintptr_t {
            THREAD          = 0b1,  // if set, pointer is not a real child of this node
            RED             = 0b10,  // colors this node during red-black balancing
            MASK            = THREAD | RED,
        };

        uintptr_t m_prev = 0;
        uintptr_t m_next = 0;

    public:
        using value_type = T;
        using hash_type = void;
        using less_type = Less;

        /* The `prev` pointer can be read from and assigned to just like normal. */
        __declspec(property(get = _get_prev, put = _set_prev))
        node* prev;
        [[nodiscard, gnu::always_inline]] node* _get_prev() const noexcept {
            return reinterpret_cast<node*>(m_prev & ~MASK);
        }
        [[gnu::always_inline]] void _set_prev(node* p) noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (reinterpret_cast<uintptr_t>(p) & MASK) {
                    throw MemoryError(
                        "node->prev cannot be assigned to a pointer that is not "
                        "8-byte aligned: " + std::to_string(
                            reinterpret_cast<uintptr_t>(p)
                        )
                    );
                }
            }
            m_prev &= MASK;
            m_prev |= reinterpret_cast<uintptr_t>(p);
        }

        /* The `next` pointer can be read from and assigned to just like normal. */
        __declspec(property(get = _get_next, put = _set_next))
        node* next;
        [[nodiscard, gnu::always_inline]] node* _get_next() const noexcept {
            return reinterpret_cast<node*>(m_next & ~MASK);
        }
        [[gnu::always_inline]] void _set_next(node* p) noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (reinterpret_cast<uintptr_t>(p) & MASK) {
                    throw MemoryError(
                        "node->next cannot be assigned to a pointer that is not "
                        "8-byte aligned: " + std::to_string(
                            reinterpret_cast<uintptr_t>(p)
                        )
                    );
                }
            }
            m_next &= MASK;
            m_next |= reinterpret_cast<uintptr_t>(p);
        }

        /* `node->prev_thread` uses an internal tag bit to tell whether the `prev`
        pointer is a BST thread, and not a true child of this node. */
        __declspec(property(get = _get_prev_thread, put = _set_prev_thread))
        bool prev_thread;
        [[nodiscard, gnu::always_inline]] bool _get_prev_thread() const noexcept {
            return m_prev & Flags::THREAD;
        }
        [[gnu::always_inline]] void _set_prev_thread(bool b) noexcept {
            m_prev &= ~Flags::THREAD;
            m_prev |= Flags::THREAD * b;
        }

        /* `node->next_thread` uses an internal tag bit to tell whether the `next`
        pointer is a BST thread, and not a true child of this node. */
        __declspec(property(get = _get_next_thread, put = _set_next_thread))
        bool next_thread;
        [[nodiscard, gnu::always_inline]] bool _get_next_thread() const noexcept {
            return m_next & Flags::THREAD;
        }
        [[gnu::always_inline]] void _set_next_thread(bool b) noexcept {
            m_next &= ~Flags::THREAD;
            m_next |= Flags::THREAD * b;
        }

        /* `node->red` returns the state of an extra color bit stored in the `prev`
        pointer. */
        __declspec(property(get = _get_red, put = _set_red))
        bool red;
        [[nodiscard, gnu::always_inline]] bool _get_red() const noexcept {
            return m_prev & Flags::RED;
        }
        [[gnu::always_inline]] void _set_red(bool b) noexcept {
            m_prev &= ~Flags::RED;
            m_prev |= Flags::RED * b;
        }

        value_type value;

        template <typename... Args> requires (std::constructible_from<value_type, Args...>)
        node(Args&&... args) noexcept(noexcept(value_type(std::forward<Args>(args)...))) :
            value(std::forward<Args>(args)...)
        {}

        node(const node& other) noexcept(noexcept(value_type(other.value))) :
            value(other.value)
        {}

        node(node&& other) noexcept(noexcept(value_type(std::move(other.value)))) :
            value(std::move(other.value))
        {}

        node& operator=(const node& other) noexcept(noexcept(value = other.value)) {
            if (this != &other) {
                value = other.value;
            }
            return *this;
        }

        node& operator=(node&& other) noexcept(noexcept(value = std::move(other.value))) {
            if (this != &other) {
                value = std::move(other.value);
            }
            return *this;
        }

        template <typename U>
            requires (
                std::is_invocable_r_v<
                    bool,
                    less_type,
                    const value_type&,
                    const unwrap_node<U>&
                >
            )
        [[nodiscard]] friend bool operator<(const node& lhs, const U& rhs) noexcept(
            std::is_nothrow_invocable_r_v<
                bool,
                less_type,
                const value_type&,
                const unwrap_node<U>&
            >
        ) {
            if constexpr (meta::linked_node<U>) {
                return less_type{}(lhs.value, rhs.value);
            } else {
                return less_type{}(lhs.value, rhs);
            }
        }

        template <typename U>
            requires (std::is_invocable_r_v<
                bool,
                less_type,
                const unwrap_node<U>&,
                const value_type&
            >)
        [[nodiscard]] friend bool operator<(const U& rhs, const node& lhs) noexcept(
            std::is_nothrow_invocable_r_v<
                bool,
                less_type,
                const unwrap_node<U>&,
                const value_type&
            >
        ) {
            if constexpr (meta::linked_node<U>) {
                return less_type{}(rhs.value, lhs.value);
            } else {
                return less_type{}(rhs, lhs.value);
            }
        }
    };

    /* Node type for hashed, non-BST nodes. */
    template <typename T, typename Hash>
    struct alignas(NODE_ALIGNMENT) node<T, Hash, void> : node_tag {
        using value_type = T;
        using hash_type = Hash;
        using less_type = void;

        node* prev = nullptr;
        node* next = nullptr;
        value_type value;
        size_t hash;

        template <typename... Args> requires (std::constructible_from<value_type, Args...>)
        node(Args&&... args) noexcept(
            noexcept(value_type(std::forward<Args>(args)...)) &&
            noexcept(hash_type{}(value))
        ) :
            value(std::forward<Args>(args)...),
            hash(hash_type{}(this->value))
        {}

        node(const node& other) noexcept(noexcept(value_type(other.value))) :
            value(other.value),
            hash(other.hash)
        {}

        node(node&& other) noexcept(noexcept(value_type(std::move(other.value)))) :
            value(std::move(other.value)),
            hash(other.hash)
        {}

        node& operator=(const node& other) noexcept(noexcept(value = other.value)) {
            if (this != &other) {
                value = other.value;
                hash = other.hash;
            }
            return *this;
        }

        node& operator=(node&& other) noexcept(noexcept(value = std::move(other.value))) {
            if (this != &other) {
                value = std::move(other.value);
                hash = other.hash;
            }
            return *this;
        }
    };

    /* Node type for non-hashed, non-BST nodes. */
    template <typename T>
    struct alignas(NODE_ALIGNMENT) node<T, void, void> : node_tag {
        using value_type = T;
        using hash_type = void;
        using less_type = void;

        node* prev = nullptr;
        node* next = nullptr;
        value_type value;

        template <typename... Args> requires (std::constructible_from<value_type, Args...>)
        node(Args&&... args) noexcept(noexcept(value_type(std::forward<Args>(args)...))) :
            value(std::forward<Args>(args)...)
        {}

        node(const node& other) noexcept(noexcept(value_type(other.value))) :
            value(other.value)
        {}

        node(node&& other) noexcept(noexcept(value_type(std::move(other.value)))) :
            value(std::move(other.value))
        {}

        node& operator=(const node& other) noexcept(noexcept(value = other.value)) {
            if (this != &other) {
                value = other.value;
            }
            return *this;
        }

        node& operator=(node&& other) noexcept(noexcept(value = std::move(other.value))) {
            if (this != &other) {
                value = std::move(other.value);
            }
            return *this;
        }
    };

    /* A simple, bidirectional iterator over a linked list data structure. */
    template <meta::linked_node Node> requires (!std::is_reference_v<Node>)
    struct node_iterator {
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = Node;
        using reference = Node&;
        using pointer = Node*;

    private:
        Node* curr;

    public:
        node_iterator(Node* curr = nullptr) noexcept : curr(curr) {}

        [[nodiscard]] Node& operator*() noexcept { return *curr; }
        [[nodiscard]] const Node& operator*() const noexcept { return *curr; }
        [[nodiscard]] Node* operator->() noexcept { return curr; }
        [[nodiscard]] const Node* operator->() const noexcept { return curr; }

        node_iterator& operator++() noexcept {
            curr = curr->next;
            return *this;
        }

        [[nodiscard]] node_iterator operator++(int) noexcept {
            node_iterator temp = *this;
            ++(*this);
            return temp;
        }

        node_iterator& operator--() noexcept {
            curr = curr->prev;
            return *this;
        }

        [[nodiscard]] node_iterator operator--(int) noexcept {
            node_iterator temp = *this;
            --(*this);
            return temp;
        }

        [[nodiscard]] bool operator==(const node_iterator& other) const noexcept {
            return curr == other.curr;
        }

        [[nodiscard]] bool operator!=(const node_iterator& other) const noexcept {
            return curr != other.curr;
        }
    };

    /* A reversed version of the above iterator. */
    template <meta::linked_node Node> requires (!std::is_reference_v<Node>)
    struct reverse_node_iterator {
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = Node;
        using reference = Node&;
        using pointer = Node*;

    private:
        Node* curr;

    public:
        reverse_node_iterator(Node* curr = nullptr) noexcept : curr(curr) {}

        [[nodiscard]] Node& operator*() noexcept { return *curr; }
        [[nodiscard]] const Node& operator*() const noexcept { return *curr; }
        [[nodiscard]] Node* operator->() noexcept { return curr; }
        [[nodiscard]] const Node* operator->() const noexcept { return curr; }

        reverse_node_iterator& operator++() noexcept {
            curr = curr->prev;
            return *this;
        }

        [[nodiscard]] reverse_node_iterator operator++(int) noexcept {
            reverse_node_iterator temp = *this;
            ++(*this);
            return temp;
        }

        reverse_node_iterator& operator--() noexcept {
            curr = curr->next;
            return *this;
        }

        [[nodiscard]] reverse_node_iterator operator--(int) noexcept {
            reverse_node_iterator temp = *this;
            --(*this);
            return temp;
        }

        [[nodiscard]] bool operator==(const reverse_node_iterator& other) const noexcept {
            return curr == other.curr;
        }

        [[nodiscard]] bool operator!=(const reverse_node_iterator& other) const noexcept {
            return curr != other.curr;
        }
    };

    /* Helper class adds an extra `root` pointer for BST views, in addition to `head`
    and `tail`. */
    template <typename Node>
    struct view_base : view_tag {};
    template <meta::bst_node Node>
    struct view_base<Node> : view_tag {
        Node* root = nullptr;
    };

    /* A wrapper around a node allocator for a linked list data structure or unhashed
    BST, which keeps track of the head, tail, (root), size, and capacity of the
    underlying array, along with several helper methods for low-level memory
    management.

    The memory is laid out as a contiguous array of nodes similar to a `std::vector`,
    but with added `prev` and `next` pointers between nodes.  The array block is
    allocated using the provided allocator, and is filled up from left to right,
    maintaining contiguous order.  If a node is removed from the list, it is added to a
    singly-linked free list composed of the `next` pointers of the removed nodes, which
    is checked when allocating new nodes.  This allows the contiguous condition to be
    violated, fragmenting the array and allowing O(1) reordering.  In order to maximize
    cache locality, the array is trivially defragmented (returned to contiguous order)
    whenever it is copied, emptied, or resized, as part of the same operation.  Manual,
    O(n) defragments can also be done via the `defragment()` method.

    If `N` is greater than zero, then the array will be allocated on the stack with a
    fixed capacity of `N` elements.  Such an array cannot be resized and will throw an
    exception if filled past capacity, or if the `reserve()`, `defragment()`, or
    `shrink()` methods are called.  All elements in the array are guaranteed to have
    stable addresses for their full duration within the list, and defragmentation will
    only occur on copy/move or upon removing the last element of the list. */
    template <meta::unqualified Node, size_t N, meta::unqualified Alloc>
        requires (meta::linked_node<Node> && meta::allocator_for<Alloc, Node>)
    struct list_view : view_base<Node> {
        using allocator_type = Alloc;
        using size_type = size_t;
        using difference_type = std::ptrdiff_t;
        using value_type = Node;
        using reference = Node&;
        using const_reference = const Node&;
        using pointer = Node*;
        using const_pointer = const Node*;
        using iterator = node_iterator<Node>;
        using const_iterator = node_iterator<const Node>;
        using reverse_iterator = reverse_node_iterator<Node>;
        using const_reverse_iterator = reverse_node_iterator<const Node>;

        /* Indicates whether the data structure has a fixed capacity (true) or supports
        reallocations (false).  If true, then the contents are guaranteed to retain
        stable addresses. */
        static constexpr bool STATIC = N > 0;

        /* The minimum size for a dynamic array, to prevent thrashing.  This has no
        effect if the array has a fixed capacity (N > 0) */
        static constexpr size_t MIN_SIZE = 8;

        /* Controls whether `a = b` causes `a` to copy the allocator from `b`.
        Equivalent to the same field on `std::allocator_traits<Alloc>`.  If false
        (the default), then `a` will retain its original allocator, and will only copy
        the contents of `b`, and not the allocator itself. */
        static constexpr bool PROPAGATE_ON_COPY_ASSIGNMENT =
            std::allocator_traits<Alloc>::propagate_on_container_copy_assignment::value;

        /* Controls whether `a = std::move(b)` causes `a` to move the allocator from
        `b`.  Equivalent to the same field on `std::allocator_traits<Alloc>`.  If false
        (the default), then `a` will retain its original allocator, and will only
        adopt the new array if the allocators compare equal.  Otherwise, the existing
        allocator will be used to create a new array, into which the contents are
        moved on an elementwise basis. */
        static constexpr bool PROPAGATE_ON_MOVE_ASSIGNMENT =
            std::allocator_traits<Alloc>::propagate_on_container_move_assignment::value;

        /* Controls whether `swap(a, b)` causes the underlying allocators to also be
        swapped.  Equivalent to the same field on `std::allocator_traits<Alloc>`.  If
        false (the default), then `a` and `b` will retain their original allocators,
        and only swap the underlying array if they compare equal.  Otherwise, a 3-way
        move will be performed using a temporary list. */
        static constexpr bool PROPAGATE_ON_SWAP =
            std::allocator_traits<Alloc>::propagate_on_container_swap::value;

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
            Node* freelist = nullptr;
            explicit operator bool() const noexcept { return M; }
        };

        /* A dynamic array, which can grow and shrink as needed. */
        template <>
        struct Array<0> {
            size_t capacity = 0;
            size_t size = 0;
            Node* data = nullptr;
            Node* freelist = nullptr;
            explicit operator bool() const noexcept { return capacity; }
        };

        Alloc allocator;
        Array<N> array;

        Array<N> allocate(size_t n) {
            static_assert(N == 0);
            size_t cap = n < MIN_SIZE ? MIN_SIZE : n;
            Array<N> array {
                .capacity = cap,
                .size = 0,
                .data = std::allocator_traits<Alloc>::allocate(allocator, cap),
                .freelist = nullptr
            };
            if (!array.data) {
                throw MemoryError();
            }
            if constexpr (DEBUG) {
                if (reinterpret_cast<uintptr_t>(array.data) & (NODE_ALIGNMENT - 1)) {
                    throw MemoryError(
                        "allocated array is not " + static_str<>::from_int<NODE_ALIGNMENT> +
                        "-byte aligned: " + std::to_string(
                            reinterpret_cast<uintptr_t>(array.data)
                        )
                    );
                }
            }
            return array;
        }

        void deallocate(Array<N>& array) noexcept(
            noexcept(std::allocator_traits<Alloc>::deallocate(
                allocator,
                array.data,
                array.capacity
            ))
        ) {
            static_assert(N == 0);
            std::allocator_traits<Alloc>::deallocate(
                allocator,
                array.data,
                array.capacity
            );
            array.capacity = 0;
            array.size = 0;
            array.data = nullptr;
            array.freelist = nullptr;
        }

        template <typename... Args> requires (std::constructible_from<Node, Args...>)
        void construct(Node* p, Args&&... args) noexcept(
            noexcept(std::allocator_traits<Alloc>::construct(
                allocator,
                p,
                std::forward<Args>(args)...
            ))
        ) {
            std::allocator_traits<Alloc>::construct(
                allocator,
                p,
                std::forward<Args>(args)...
            );
        }

        void destroy(Node* p) noexcept(
            noexcept(std::allocator_traits<Alloc>::destroy(allocator, p))
        ) {
            std::allocator_traits<Alloc>::destroy(allocator, p);
        }

        void destroy_list(Array<N>& array, Node* head) noexcept(
            noexcept(destroy(head)) && noexcept(deallocate(array))
        ) {
            if constexpr (N == 0) {
                if (array) {
                    Node* curr = head;
                    while (curr) {
                        Node* next = curr->next;
                        destroy(curr);
                        curr = next;
                    }
                    deallocate(array);
                }
            } else {
                Node* curr = head;
                while (curr) {
                    Node* next = curr->next;
                    destroy(curr);
                    curr = next;
                }
                array.size = 0;
                array.freelist = nullptr;
            }
            this->head = nullptr;
            this->tail = nullptr;
            if constexpr (meta::bst_node<Node>) {
                this->root = nullptr;
            }
        }

        /// TODO: resize(), as well as copy/move/swap will have to maintain the flags
        /// when copying/moving the nodes.  This is a bit tricky, but shouldn't be
        /// too bad, and allows the list allocator to also be used as a BST allocator
        /// at the same time.  It will also have to do the same bookeeping for `root`,
        /// which is a bit more complicated, but still doable.

        void resize(size_t cap) {
            static_assert(N == 0);
            if (cap < array.size) {
                throw MemoryError(
                    "new capacity cannot be less than current size (" +
                    std::to_string(cap) + " < " + std::to_string(array.size) + ")"
                );
            }

            // 1) if requested capacity is zero, delete the array to save space
            if (cap == 0) {
                destroy_list(array, head);
                return;
            }

            // 2) if there are no elements to transfer, just replace with the new array
            Array<N> temp = allocate(cap);
            if (array.size == 0) {
                destroy_list(array, head);
                array = temp;
                return;
            }

            // 3) transfer existing contents
            Node* new_head = temp.data;
            Node* new_tail = new_head;
            Node* new_root;
            try {
                Node* prev = head;
                Node* curr = head->next;
                bool prev_thread;
                bool next_thread;
                bool red;
                if constexpr (meta::bst_node<Node>) {
                    prev_thread = head->prev_thread;
                    next_thread = head->next_thread;
                    red = head->red;
                }

                // 3a) move the underlying value and initialize prev/next to null,
                // copying the original link/color info
                construct(new_head, std::move(*head));
                if constexpr (meta::bst_node<Node>) {
                    new_head->prev_thread = prev_thread;
                    new_head->next_thread = next_thread;
                    new_head->red = red;
                    if (head == this->root) {
                        new_root = new_head;
                    }
                }

                // 3b) destroy the moved-from value, and then posthumously restore
                // original link/color info
                destroy(head);
                head->prev = nullptr;
                head->next = curr;
                if constexpr (meta::bst_node<Node>) {
                    head->prev_thread = prev_thread;
                    head->next_thread = next_thread;
                    head->red = red;
                }
                ++temp.size;

                // 3c) continue with intermediate links
                while (curr) {
                    if constexpr (meta::bst_node<Node>) {
                        prev_thread = curr->prev_thread;
                        next_thread = curr->next_thread;
                        red = curr->red;
                    }
                    Node* next = curr->next;

                    // 3d) move the value into the node at the end of the contiguously-
                    // allocated section, link it to the previous node, and copy the
                    // original link/color flags
                    new_tail->next = temp.data + temp.size;
                    construct(new_tail->next, std::move(*curr));
                    new_tail->next->prev = new_tail;
                    new_tail = new_tail->next;
                    if constexpr (meta::bst_node<Node>) {
                        new_tail->prev_thread = prev_thread;
                        new_tail->next_thread = next_thread;
                        new_tail->red = red;
                        if (curr == this->root) {
                            new_root = new_tail;
                        }
                    }

                    // 3e) destroy the moved-from node, and then posthumously restore
                    // original link/color info
                    destroy(curr);
                    curr->prev = prev;
                    curr->next = next;
                    if constexpr (meta::bst_node<Node>) {
                        curr->prev_thread = prev_thread;
                        curr->next_thread = next_thread;
                        curr->red = red;
                    }

                    // 3f) advance for next iteration
                    prev = curr;
                    curr = next;
                    ++temp.size;
                }

            // 4) If a move constructor fails, replace the previous values
            } catch (...) {
                Node* prev = nullptr;
                Node* curr = head;
                bool prev_thread;
                bool next_thread;
                bool red;
                for (size_t i = 0; i < temp.size; ++i) {
                    if constexpr (meta::bst_node<Node>) {
                        prev_thread = curr->prev_thread;
                        next_thread = curr->next_thread;
                        red = curr->red;
                    }
                    Node* next = curr->next;
                    Node* node = temp.data + i;
                    construct(curr, std::move(*node));
                    destroy(node);
                    curr->prev = prev;
                    curr->next = next;
                    if constexpr (meta::bst_node<Node>) {
                        curr->prev_thread = prev_thread;
                        curr->next_thread = next_thread;
                        curr->red = red;
                    }
                    prev = curr;
                    curr = next;
                }
                deallocate(temp);
                throw;
            }

            // 5) replace the old array
            if (array) {
                deallocate(array);
            }
            array = temp;
            head = new_head;
            tail = new_tail;
            if constexpr (meta::bst_node<Node>) {
                this->root = new_root;
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
        Node* head = nullptr;
        Node* tail = nullptr;

        template <typename... Args> requires (std::constructible_from<Alloc, Args...>)
        list_view(Args&&... args) noexcept(
            noexcept(Alloc(std::forward<Args>(args)...))
        ) :
            allocator(std::forward<Args>(args)...)
        {}

        list_view(const list_view& other) noexcept(
            noexcept(Alloc(other.allocator)) &&
            N &&
            noexcept(construct(head, *other.head)) &&
            (!meta::bst_node<Node> || !DEBUG)
        ) :
            allocator(other.allocator)
        {
            // if the other array is empty, then we can avoid allocating
            if (other.empty()) {
                return;
            }

            // only allocate enough space for the other list's contents
            if constexpr (N == 0) {
                array = allocate(other.size());
            }
            head = array.data;
            tail = head;

            // construct the first node, then continue with intermediate links
            try {
                construct(head, *other.head);
                if constexpr (meta::bst_node<Node>) {
                    head->prev_thread = other.head->prev_thread;
                    head->next_thread = other.head->next_thread;
                    head->red = other.head->red;
                    if (other.head == other.root) {
                        this->root = head;
                    }
                }
                ++array.size;
                Node* curr = other.head->next;
                while (curr) {
                    tail->next = array.data + array.size;
                    construct(tail->next, *curr);
                    tail->next->prev = tail;
                    tail = tail->next;
                    if constexpr (meta::bst_node<Node>) {
                        tail->prev_thread = curr->prev_thread;
                        tail->next_thread = curr->next_thread;
                        tail->red = curr->red;
                        if (curr == other.root) {
                            this->root = tail;
                        }
                    }
                    curr = curr->next;
                    ++array.size;
                }

            // If a copy constructor fails, destroy nodes that have been added
            } catch (...) {
                for (size_t i = 0; i < array.size; ++i) {
                    destroy(array.data + i);
                }
                if constexpr (N == 0) {
                    deallocate(array);
                }
                throw;
            }
        }

        list_view(list_view&& other) noexcept (
            noexcept(Alloc(std::move(other.allocator))) &&
            (N == 0 || (
                noexcept(construct(head, std::move(*other.head))) &&
                (!meta::bst_node<Node> || !DEBUG)
            ))
        ) :
            allocator(std::move(other.allocator))
        {
            // if the array has fixed size, then we need to move each element manually
            if constexpr (N) {
                if (other.empty()) {
                    return;
                }
                head = array.data;
                tail = head;

                // construct the first node, then continue with intermediate links
                try {
                    construct(head, std::move(*other.head));
                    if constexpr (meta::bst_node<Node>) {
                        head->prev_thread = other.head->prev_thread;
                        head->next_thread = other.head->next_thread;
                        head->red = other.head->red;
                        if (other.head == other.root) {
                            this->root = head;
                        }
                    }
                    ++array.size;
                    Node* curr = other.head->next;
                    while (curr) {
                        tail->next = array.data + array.size;
                        construct(tail->next, std::move(*curr));
                        tail->next->prev = tail;
                        tail = tail->next;
                        if constexpr (meta::bst_node<Node>) {
                            tail->prev_thread = curr->prev_thread;
                            tail->next_thread = curr->next_thread;
                            tail->red = curr->red;
                            if (curr == other.root) {
                                this->root = tail;
                            }
                        }
                        curr = curr->next;
                        ++array.size;
                    }

                // If a move constructor fails, replace the previous values
                } catch (...) {
                    Node* curr = other.head;
                    for (size_t i = 0; i < array.size; ++i) {
                        Node* node = array.data + i;
                        *curr = std::move(*node);
                        destroy(node);
                        curr = curr->next;
                    }
                    throw;
                }

            // otherwise, we can just transfer ownership
            } else {
                array = other.array;
                head = other.head;
                tail = other.tail;
                if constexpr (meta::bst_node<Node>) {
                    this->root = other.root;
                }
                other.array.capacity = 0;
                other.array.size = 0;
                other.array.data = nullptr;
                other.array.freelist = nullptr;
                other.head = nullptr;
                other.tail = nullptr;
                if constexpr (meta::bst_node<Node>) {
                    other.root = nullptr;
                }
            }
        }

        list_view& operator=(const list_view& other) noexcept(
            N &&
            noexcept(destroy_list(array, head)) &&
            (!PROPAGATE_ON_COPY_ASSIGNMENT || noexcept(allocator = other.allocator)) &&
            noexcept(construct(head, *other.head)) &&
            (!meta::bst_node<Node> || !DEBUG)
        ) {
            if (this == &other) {
                return *this;
            }

            // delete the old array using current allocator, then copy if propagating
            destroy_list(array, head);
            if constexpr (PROPAGATE_ON_COPY_ASSIGNMENT) {
                allocator = other.allocator;
            }

            // if other list is empty, return without allocating
            if (other.empty()) {
                return *this;
            }

            // otherwise, allocate only enough space to fit the other list's contents
            if constexpr (N == 0) {
                array = allocate(other.size());
            }
            head = array.data;
            tail = head;

            // construct the first node, then continue with intermediate links
            try {
                construct(head, *other.head);
                if constexpr (meta::bst_node<Node>) {
                    head->prev_thread = other.head->prev_thread;
                    head->next_thread = other.head->next_thread;
                    head->red = other.head->red;
                    if (other.head == other.root) {
                        this->root = head;
                    }
                }
                ++array.size;
                Node* curr = other.head->next;
                while (curr) {
                    tail->next = array.data + array.size;
                    construct(tail->next, *curr);
                    tail->next->prev = tail;
                    tail = tail->next;
                    if constexpr (meta::bst_node<Node>) {
                        tail->prev_thread = curr->prev_thread;
                        tail->next_thread = curr->next_thread;
                        tail->red = curr->red;
                        if (curr == other.root) {
                            this->root = tail;
                        }
                    }
                    curr = curr->next;
                    ++array.size;
                }

            // If a copy constructor fails, destroy nodes that have been added
            } catch (...) {
                for (size_t i = 0; i < array.size; ++i) {
                    destroy(array.data + i);
                }
                if constexpr (N == 0) {
                    deallocate(array);
                } else {
                    array.size = 0;
                    head = nullptr;
                    tail = nullptr;
                    if constexpr (meta::bst_node<Node>) {
                        this->root = nullptr;
                    }
                }
                throw;
            }

            return *this;
        }

        /* Move assignment operator.  If an exception occurs during the assignment, the
        target will be empty, and the assigned value will be restored to its original
        state. */
        list_view& operator=(list_view&& other) noexcept(
            (N || PROPAGATE_ON_MOVE_ASSIGNMENT) &&
            noexcept(destroy_list(array, head)) &&
            (!PROPAGATE_ON_MOVE_ASSIGNMENT || noexcept(allocator = std::move(other.allocator))) &&
            noexcept(construct(head, std::move(*other.head))) &&
            (!meta::bst_node<Node> || !DEBUG)
        ) {
            if (this == &other) {
                return *this;
            }

            // delete the old array using current allocator
            destroy_list(array, head);

            // if the array has fixed capacity, then moves must be done elementwise
            if constexpr (N) {
                if constexpr (PROPAGATE_ON_MOVE_ASSIGNMENT) {
                    allocator = std::move(other.allocator);
                }
                if (other.empty()) {
                    return *this;
                }
                head = array.data;
                tail = head;

                // construct the first node, then continue with intermediate links
                try {
                    construct(head, std::move(*other.head));
                    if constexpr (meta::bst_node<Node>) {
                        head->prev_thread = other.head->prev_thread;
                        head->next_thread = other.head->next_thread;
                        head->red = other.head->red;
                        if (other.head == other.root) {
                            this->root = head;
                        }
                    }
                    ++array.size;
                    Node* curr = other.head->next;
                    while (curr) {
                        tail->next = array.data + array.size;
                        construct(tail->next, std::move(*curr));
                        tail->next->prev = tail;
                        tail = tail->next;
                        if constexpr (meta::bst_node<Node>) {
                            tail->prev_thread = curr->prev_thread;
                            tail->next_thread = curr->next_thread;
                            tail->red = curr->red;
                            if (curr == other.root) {
                                this->root = tail;
                            }
                        }
                        curr = curr->next;
                        ++array.size;
                    }

                // If a move constructor fails, replace the previous values
                } catch (...) {
                    Node* node = other.head;
                    for (size_t i = 0; i < array.size; ++i) {
                        Node* curr = array.data + i;
                        *node = std::move(*curr);
                        destroy(curr);
                        node = node->next;
                    }
                    array.size = 0;
                    head = nullptr;
                    tail = nullptr;
                    if constexpr (meta::bst_node<Node>) {
                        this->root = nullptr;
                    }
                    throw;
                }

            // dynamic moves are trivial if propagating the other allocator
            } else if constexpr (PROPAGATE_ON_MOVE_ASSIGNMENT) {
                allocator = std::move(other.allocator);
                array = other.array;
                head = other.head;
                tail = other.tail;
                if constexpr (meta::bst_node<Node>) {
                    this->root = other.root;
                }
                other.array.capacity = 0;
                other.array.size = 0;
                other.array.data = nullptr;
                other.array.freelist = nullptr;
                other.head = nullptr;
                other.tail = nullptr;
                if constexpr (meta::bst_node<Node>) {
                    other.root = nullptr;
                }

            // otherwise, allocators must compare equal to trivially move
            } else {
                if (allocator == other.allocator) {
                    array = other.array;
                    head = other.head;
                    tail = other.tail;
                    if constexpr (meta::bst_node<Node>) {
                        this->root = other.root;
                    }
                    other.array.capacity = 0;
                    other.array.size = 0;
                    other.array.data = nullptr;
                    other.array.freelist = nullptr;
                    other.head = nullptr;
                    other.tail = nullptr;
                    if constexpr (meta::bst_node<Node>) {
                        other.root = nullptr;
                    }
                    return *this;
                }

                // if the other list is empty, return without allocating
                if (other.empty()) {
                    return *this;
                }
                array = allocate(other.size());
                head = array.data;
                tail = head;

                // construct the first node, then continue with intermediate links
                try {
                    construct(head, std::move(*other.head));
                    if constexpr (meta::bst_node<Node>) {
                        head->prev_thread = other.head->prev_thread;
                        head->next_thread = other.head->next_thread;
                        head->red = other.head->red;
                        if (other.head == other.root) {
                            this->root = head;
                        }
                    }
                    ++array.size;
                    Node* curr = other.head->next;
                    while (curr) {
                        tail->next = array.data + array.size;
                        construct(tail->next, std::move(*curr));
                        tail->next->prev = tail;
                        tail = tail->next;
                        if constexpr (meta::bst_node<Node>) {
                            tail->prev_thread = curr->prev_thread;
                            tail->next_thread = curr->next_thread;
                            tail->red = curr->red;
                            if (curr == other.root) {
                                this->root = tail;
                            }
                        }
                        curr = curr->next;
                        ++array.size;
                    }

                // If a move constructor fails, replace the previous values
                } catch (...) {
                    Node* node = other.head;
                    for (size_t i = 0; i < array.size; ++i) {
                        Node* curr = array.data + i;
                        *node = std::move(*curr);
                        destroy(curr);
                        node = node->next;
                    }
                    deallocate(array);
                    head = nullptr;
                    tail = nullptr;
                    if constexpr (meta::bst_node<Node>) {
                        this->root = nullptr;
                    }
                    throw;
                }
            }
            return *this;
        }

        ~list_view() noexcept(noexcept(destroy_list(array, head))) {
            destroy_list(array, head);
        }

        /* Swap two lists as cheaply as possible.  If an exception occurs, both
        operands will be restored to their original state. */
        void swap(list_view& other) noexcept(
            N ? (
                noexcept(Alloc(std::move(*this))) &&
                noexcept(*this = std::move(other)) &&
                noexcept(other = std::move(*this))
            ) : (
                std::is_nothrow_swappable_v<Alloc> &&
                std::is_nothrow_swappable_v<Array<N>> &&
                std::is_nothrow_swappable_v<Node*> &&
                PROPAGATE_ON_SWAP || (
                    noexcept(Alloc(std::move(*this))) &&
                    noexcept(other = std::move(*this)) &&
                    noexcept(*this = std::move(other))
                )
            )
        ) {
            using std::swap;
            if (this == &other) {
                return;
            }

            // if the array has fixed capacity, then swaps must be done elementwise
            if constexpr (N) {
                // 1) move the current list into a temporary
                list_view temp(std::move(*this));

                // 2) move the other list into this list
                try {
                    *this = std::move(other);

                    // 3) move temp into the other list
                    try {
                        other = std::move(temp);

                    // 3a) if a move constructor fails, other will be empty and temp
                    // will be unchanged, so we must first move this back into other,
                    // and then move temp into this
                    } catch(...) {
                        other = std::move(*this);
                        *this = std::move(temp);
                        throw;
                    }

                // 2a) if a move constructor fails, this will be empty and other will
                // be unchanged, so we must move temp back into this list
                } catch (...) {
                    *this = std::move(temp);
                    throw;
                }

            // swaps are trivial if we're propagating the allocators
            } else if constexpr (PROPAGATE_ON_SWAP) {
                swap(allocator, other.allocator);
                swap(array, other.array);
                swap(head, other.head);
                swap(tail, other.tail);

            // otherwise, the allocators have to compare equal to trivially swap
            } else {
                if (allocator == other.allocator) {
                    swap(allocator, other.allocator);
                    swap(array, other.array);
                    swap(head, other.head);
                    swap(tail, other.tail);
                    return;
                }

                // this proceeds just like fixed-capacity arrays
                list_view temp(std::move(*this));
                try {
                    *this = std::move(other);
                    try {
                        other = std::move(temp);
                    } catch(...) {
                        other = std::move(*this);
                        *this = std::move(temp);
                        throw;
                    }
                } catch (...) {
                    *this = std::move(temp);
                    throw;
                }
            }
        }

        /* The number of nodes in the list. */
        [[nodiscard]] size_t size() const noexcept { return array.size; }

        /* True if the list has zero size.  False otherwise. */
        [[nodiscard]] bool empty() const noexcept { return !array.size; }

        /* True if the list has nonzero size.  False otherwise. */
        [[nodiscard]] explicit operator bool() const noexcept { return array.size; }

        /* The total number of nodes the array can store before resizing. */
        [[nodiscard]] size_t capacity() const noexcept { return array.capacity; }

        /* Estimate the overall memory usage of the list in bytes. */
        [[nodiscard]] size_t memory_usage() const noexcept {
            return sizeof(list_view) + capacity() * sizeof(Node);
        }

        /* Initialize a new node for the list.  The result has null `prev` and `next`
        pointers, and is initially disconnected from all other nodes, though it is
        included in `size()`.  This can cause the array to grow if it does not have a
        fixed capacity, otherwise this method will throw a `MemoryError`. */
        template <typename... Args> requires (std::constructible_from<Node, Args...>)
        [[nodiscard]] Node* create(Args&&... args) {
            // check free list for recycled nodes
            if (array.freelist) {
                Node* node = array.freelist;
                Node* next = array.freelist->next;
                construct(node, std::forward<Args>(args)...);
                ++array.size;
                array.freelist = next;
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
            construct(node, std::forward<Args>(args)...);
            ++array.size;
            return node;
        }

        /* Destroy a node from the list, inserting it into the free list.  Note that
        the node should be disconnected from all other nodes before calling this
        method, and the node must have been allocated from this view.  Trivially
        defragments the array if this is the last node in the list. */
        void recycle(Node* node) noexcept(!DEBUG && noexcept(destroy(node))) {
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
            destroy(node);
            --array.size;

            // if this was the last node, reset the freelist to naturally defragment
            // the array.  Otherwise, append to freelist.
            if (!array.size) {
                array.freelist = nullptr;
            } else {
                node->next = array.freelist;
                array.freelist = node;
            }
        }

        /* Remove all nodes from the list, resetting the size to zero, but leaving the
        capacity unchanged.  Also trivially defragments the array. */
        void clear() noexcept(noexcept(destroy(head))) {
            Node* curr = head;
            while (curr) {
                Node* next = curr->next;
                destroy(curr);
                curr = next;
            }
            array.size = 0;
            array.freelist = nullptr;
            head = nullptr;
            tail = nullptr;
            if constexpr (meta::bst_node<Node>) {
                this->root = nullptr;
            }
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
        array grows or shrinks, and may be triggered manually as an optimization.
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

        [[nodiscard]] iterator begin() noexcept { return {head}; }
        [[nodiscard]] const_iterator begin() const noexcept { return {head}; }
        [[nodiscard]] const_iterator cbegin() const noexcept { return {head}; }
        [[nodiscard]] iterator end() noexcept { return {nullptr}; }
        [[nodiscard]] const_iterator end() const noexcept { return {nullptr}; }
        [[nodiscard]] const_iterator cend() const noexcept { return {nullptr}; }
        [[nodiscard]] reverse_iterator rbegin() noexcept { return {tail}; }
        [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return {tail}; }
        [[nodiscard]] const_reverse_iterator crbegin() const noexcept { return {tail}; }
        [[nodiscard]] reverse_iterator rend() noexcept { return {nullptr}; }
        [[nodiscard]] const_reverse_iterator rend() const noexcept { return {nullptr}; }
        [[nodiscard]] const_reverse_iterator crend() const noexcept { return {nullptr}; }

        /* Return an iterator to the specified index of the list.  Allows Python-style
        negative indexing, and throws an `IndexError` if the index is out of bounds
        after normalization.  Has a time compexity of O(n/2) following the links
        between each node, starting from the nearest edge. */
        [[nodiscard]] iterator operator[](ssize_t i) {
            size_t idx = normalize_index(i);

            // if the index is closer to the head of the list, start there.
            if (idx <= array.size / 2) {
                iterator it {head};
                for (size_t j = 0; j++ < idx;) {
                    ++it;
                }
                return it;
            }

            // otherwise, start at the tail of the list
            iterator it = {tail};
            ++idx;
            for (size_t j = array.size; j-- > idx;) {
                --it;
            }
            return it;
        }

        /* Return an iterator to the specified index of the list.  Allows Python-style
        negative indexing, and throws an `IndexError` if the index is out of bounds
        after normalization.  Has a time compexity of O(n/2) following the links
        between each node, starting from the nearest edge. */
        [[nodiscard]] const_iterator operator[](ssize_t i) const {
            size_t idx = normalize_index(i);

            // if the index is closer to the head of the list, start there.
            if (idx <= array.size / 2) {
                const_iterator it {head};
                for (size_t j = 0; j++ < idx;) {
                    ++it;
                }
                return it;
            }

            // otherwise, start at the tail of the list
            const_iterator it = {tail};
            ++idx;
            for (size_t j = array.size; j-- > idx;) {
                --it;
            }
            return it;
        }
    };

    /* A wrapper around a node allocator for a linked set or map. */
    template <meta::unqualified Node, size_t N, meta::unqualified Alloc>
        requires (meta::hash_node<Node> && meta::allocator_for<Alloc, Node>)
    struct hash_view : view_base<Node> {
        /// TODO: reimplement the hopscotch hashing algorithm, which can also be
        /// composed with a BST representation for automatic sorting.
    };


}  // namespace impl::linked


/// TODO: maybe all the algorithms can be placed here under the impl:: namespace?
/// They'll take up a fair amount of space, but that would mean the entire linked
/// data structure ecosystem would be stored in just a single file here, which makes
/// it easier to test with syntax highlighting.
/// -> C++ classes would then just compose whatever algorithms were needed, which
/// would be templated to work at the view level, and therefore be compatible between
/// views.



namespace impl::linked {

    template <meta::linked_view View, typename... Args>
        requires (
            !meta::bst_view<View> &&
            std::constructible_from<meta::node_type<View>, Args...>
        )
    void append(View&& view, Args&&... args) {
        auto* node = std::forward<View>(view).create(std::forward<Args>(args)...);
        if (view.tail) {
            view.tail->next = node;
            node->prev = view.tail;
            view.tail = node;
        } else {
            view.head = node;
            view.tail = node;
        }
    }

    template <meta::linked_view View, typename... Args>
        requires (
            !meta::bst_view<View> &&
            std::constructible_from<meta::node_type<View>, Args...>
        )
    void prepend(View&& view, Args&&... args) {
        auto* node = std::forward<View>(view).create(std::forward<Args>(args)...);
        if (view.head) {
            view.head->prev = node;
            node->next = view.head;
            view.head = node;
        } else {
            view.head = node;
            view.tail = node;
        }
    }

}



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
    using Node = impl::linked::node<T>;
    using Allocator = std::allocator_traits<Alloc>::template rebind_alloc<Node>;

    impl::linked::list_view<Node, N, Allocator> view;

public:

    /// TODO: separate specialization for Less = void

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
