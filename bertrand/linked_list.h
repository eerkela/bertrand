#ifndef BERTRAND_LINKED_LIST_H
#define BERTRAND_LINKED_LIST_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/static_str.h"
#include <type_traits>


namespace bertrand {


namespace impl::linked {

    struct node_tag {};
    struct view_tag {};
    struct iterator_tag {};
    struct forward_iterator_tag : iterator_tag {};
    struct reverse_iterator_tag : iterator_tag {};
    struct const_slice_tag {};
    struct slice_tag : const_slice_tag {};

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
    concept linked_iterator = meta::inherits<T, impl::linked::iterator_tag>;

    template <typename T>
    concept linked_forward_iterator =
        linked_iterator<T> && meta::inherits<T, impl::linked::forward_iterator_tag>;

    template <typename T>
    concept linked_reverse_iterator =
        linked_iterator<T> && meta::inherits<T, impl::linked::reverse_iterator_tag>;
        
    template <typename T>
    concept linked_const_slice = meta::inherits<T, impl::linked::const_slice_tag>;

    template <typename T>
    concept linked_slice =
        linked_const_slice<T> && meta::inherits<T, impl::linked::slice_tag>;

    template <typename T>
    concept linked_view = meta::inherits<T, impl::linked::view_tag>;

    template <typename T>
    concept bst_view =
        linked_view<T> && bst_node<typename std::remove_cvref_t<T>::node_type>;

    template <typename T>
    concept hash_view =
        linked_view<T> && hash_node<typename std::remove_cvref_t<T>::node_type>;

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
        struct _unwrap_node<U> { using type = std::remove_cvref_t<U>::value_type; };
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
        [[gnu::always_inline]] void _set_prev(node* p) noexcept {
            m_prev &= MASK;
            m_prev |= reinterpret_cast<uintptr_t>(p);
        }

        /* The `next` pointer can be read from and assigned to just like normal. */
        __declspec(property(get = _get_next, put = _set_next))
        node* next;
        [[nodiscard, gnu::always_inline]] node* _get_next() const noexcept {
            return reinterpret_cast<node*>(m_next & ~MASK);
        }
        [[gnu::always_inline]] void _set_next(node* p) noexcept {
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
        struct _unwrap_node<U> { using type = std::remove_cvref_t<U>::value_type; };
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
        [[gnu::always_inline]] void _set_prev(node* p) noexcept {
            m_prev &= MASK;
            m_prev |= reinterpret_cast<uintptr_t>(p);
        }

        /* The `next` pointer can be read from and assigned to just like normal. */
        __declspec(property(get = _get_next, put = _set_next))
        node* next;
        [[nodiscard, gnu::always_inline]] node* _get_next() const noexcept {
            return reinterpret_cast<node*>(m_next & ~MASK);
        }
        [[gnu::always_inline]] void _set_next(node* p) noexcept {
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
    struct node_iterator : forward_iterator_tag {
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = Node;
        using reference = Node&;
        using pointer = Node*;

    protected:
        Node* curr;

    public:
        node_iterator(Node* curr = nullptr) noexcept : curr(curr) {}

        [[nodiscard]] Node* operator*() noexcept { return curr; }
        [[nodiscard]] const Node* operator*() const noexcept { return curr; }
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
    struct reverse_node_iterator : reverse_iterator_tag {
        using iterator_category = std::bidirectional_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = Node;
        using reference = Node&;
        using pointer = Node*;

    protected:
        Node* curr;

    public:
        reverse_node_iterator(Node* curr = nullptr) noexcept : curr(curr) {}

        [[nodiscard]] Node* operator*() noexcept { return curr; }
        [[nodiscard]] const Node* operator*() const noexcept { return curr; }
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

    /* Converts a node iterator into a value iterator, hiding the node internals. */
    template <meta::unqualified T> requires (meta::linked_iterator<T>)
    struct value_iterator : T {
        using iterator_category = T::iterator_category;
        using difference_type = T::difference_type;
        using value_type = T::value_type::value_type;
        using reference = value_type&;
        using pointer = value_type*;

        using T::T;
        using T::operator=;

        template <typename V>
            requires (
                !meta::bst_node<typename T::value_type> &&
                requires(reference curr, V value) {
                    { curr = std::forward<V>(value) };
                }
            )
        value_iterator& operator=(V&& value) noexcept(
            !DEBUG &&
            noexcept(this->curr->value = std::forward<V>(value))
        ) {
            if constexpr (DEBUG) {
                if (this->curr == nullptr) {
                    throw MemoryError("cannot dereference a null iterator");
                }
            }
            this->curr->value = std::forward<V>(value);
            return *this;
        }

        [[nodiscard]] value_type& operator*() noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (this->curr == nullptr) {
                    throw MemoryError("cannot dereference a null iterator");
                }
            }
            return this->curr->value;
        }

        [[nodiscard]] const value_type& operator*() const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (this->curr == nullptr) {
                    throw MemoryError("cannot dereference a null iterator");
                }
            }
            return this->curr->value;
        }

        [[nodiscard]] value_type* operator->() noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (this->curr == nullptr) {
                    throw MemoryError("cannot dereference a null iterator");
                }
            }
            return &this->curr->value;
        }

        [[nodiscard]] const value_type* operator->() const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (this->curr == nullptr) {
                    throw MemoryError("cannot dereference a null iterator");
                }
            }
            return &this->curr->value;
        }

        value_iterator& operator++() noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (this->curr == nullptr) {
                    throw MemoryError("cannot advance a null iterator");
                }
            }
            T::operator++();
            return *this;
        }

        [[nodiscard]] value_iterator operator++(int) noexcept(!DEBUG) {
            value_iterator temp = *this;
            ++(*this);
            return temp;
        }

        template <typename V> requires (std::convertible_to<value_type&, V>)
        [[nodiscard]] operator V() noexcept(
            !DEBUG &&
            std::is_nothrow_convertible_v<value_type&, V>
        ) {
            if constexpr (DEBUG) {
                if (this->curr == nullptr) {
                    throw MemoryError("cannot dereference a null iterator");
                }
            }
            return this->curr->value;
        }

        template <typename V> requires (std::convertible_to<const value_type&, V>)
        [[nodiscard]] operator V() const noexcept(
            !DEBUG &&
            std::is_nothrow_convertible_v<const value_type&, V>
        ) {
            if constexpr (DEBUG) {
                if (this->curr == nullptr) {
                    throw MemoryError("cannot dereference a null iterator");
                }
            }
            return this->curr->value;
        }
    };

    /* Check if the index is closer to the tail of the list than the head. */
    inline bool closer_to_tail(size_t size, size_t i) noexcept {
        return i >= (size + 1) / 2;
    }

    /* Apply Python-style wraparound to an index of a container.  Throws an
    `IndexError` if the index is still out of range after normalization. */
    inline size_t normalize_index(size_t size, ssize_t i) {
        if (i < 0) {
            i += size;
        }
        if (i < 0 || i >= size) {
            throw IndexError(std::to_string(i));
        }
        return static_cast<size_t>(i);
    }

    /* A variation of `normalize_index` that also records whether the index is closer
    to the tail of the list as opposed to the head, indicating a backward traversal. */
    inline size_t normalize_index(size_t size, ssize_t i, bool& backward) {
        if (i < 0) {
            i += size;
        }
        if (i < 0 || i >= size) {
            throw IndexError(std::to_string(i));
        }
        backward = closer_to_tail(size, i);
        return static_cast<size_t>(i);
    }

    /* Apply Python-style wraparound to an index of a container.  Truncates to the
    bounds of the container if the index is still out of range after normalization.
    Note that the input size must not be zero. */
    inline size_t truncate_index(size_t size, ssize_t i) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (size == 0) {
                throw ValueError("size must not be zero");
            }
        }
        if (i < 0) {
            i += size;
            if (i < 0) {
                i = 0;
            }
        } else if (i >= ssize_t(size)) {
            i = size - 1;
        }
        return static_cast<size_t>(i);
    }

    /* A variation of `truncate_index` that also records whether the index is closer
    to the tail of the list as opposed to the head, indicating a backward traversal. */
    inline size_t truncate_index(size_t size, ssize_t i, bool& backward) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (size == 0) {
                throw ValueError("size must not be zero");
            }
        }
        if (i < 0) {
            i += size;
            if (i < 0) {
                i = 0;
            }
        } else if (i >= ssize_t(size)) {
            i = size - 1;
        }
        backward = closer_to_tail(size, i);
        return static_cast<size_t>(i);
    }

    /* Normalize a slice of the form `{start, stop, step}`, where each element is
    optional, against a container of a specified size such that no backtracking occurs
    when iterating over it. */
    struct normalize_slice {
        ssize_t step = 0;  // normalized step
        ssize_t start = 0;  // normalized start
        ssize_t stop = 0;  // normalized stop
        size_t length = 0;  // total number of elements in the slice
        size_t abs_step = 0;  // absolute value of step (for iterating)
        size_t first = 0;  // first index included in the slice
        size_t last = 0;  // last index included in the slice
        bool inverted = false;  // true if iterating over the slice in reverse order
        bool backward = false;  // true if iterating from the tail of the list

        normalize_slice(
            ssize_t size,
            std::optional<ssize_t> start = std::nullopt,
            std::optional<ssize_t> stop = std::nullopt
        ) noexcept : step(1) {
            normalize(size, start, stop);
        }

        normalize_slice(
            ssize_t size,
            std::optional<ssize_t> start,
            std::optional<ssize_t> stop,
            std::optional<ssize_t> step
        ) {
            normalize(size, start, stop, step);
        }

        normalize_slice(
            ssize_t size,
            const std::initializer_list<std::optional<ssize_t>>& slice
        ) {
            if (slice.size() > 3) {
                throw TypeError(
                    "Slices must be of the form {start[, stop[, step]]} "
                    "(received " + std::to_string(slice.size()) + " indices)"
                );
            }
            auto it = slice.begin();
            auto end = slice.end();
            if (it == end) {
                normalize(size, std::nullopt, std::nullopt);
                return;
            }
            std::optional<ssize_t> start = *it++;
            if (it == end) {
                normalize(size, start, std::nullopt);
                return;
            }
            std::optional<ssize_t> stop = *it++;
            if (it == end) {
                normalize(size, start, stop);
                return;
            }
            std::optional<ssize_t> step = *it++;
            if (it == end) {
                normalize(size, start, stop, step);
            }
        }

    private:

        void normalize(
            ssize_t size,
            std::optional<ssize_t> start,
            std::optional<ssize_t> stop
        ) noexcept {
            // normalize start, correcting for negative indices and truncating to bounds
            if (!start) {
                this->start = 0;
            } else {
                this->start = *start;
                if (this->start < 0) {
                    this->start += size;
                    if (this->start < 0) {
                        this->start = 0;
                    }
                } else if (this->start > size) {
                    this->start = size;
                }
            }

            // normalize stop, correcting for negative indices and truncating to bounds
            if (!stop) {
                this->stop = size;
            } else {
                this->stop = *stop;
                if (this->stop < 0) {
                    this->stop += size;
                    if (this->stop < 0) {
                        this->stop = 0;
                    }
                } else if (this->stop > size) {
                    this->stop = size;
                }
            }

            normalize(size);
        }

        void normalize(
            ssize_t size,
            std::optional<ssize_t> start,
            std::optional<ssize_t> stop,
            std::optional<ssize_t> step
        ) {
            // normalize step, defaulting to 1
            this->step = step.value_or(1);
            if (this->step == 0) {
                throw ValueError("slice step cannot be zero");
            };
            bool neg = this->step < 0;

            // normalize start, correcting for negative indices and truncating to bounds
            if (!start) {
                this->start = neg ? size - 1 : 0;  // neg: size - 1 | pos: 0
            } else {
                this->start = *start;
                if (this->start < 0) {
                    this->start += size;
                    if (this->start < 0) {
                        this->start = -neg;  // neg: -1 | pos: 0 
                    }
                } else if (this->start >= size) {
                    this->start = size - neg;  // neg: size - 1 | pos: size
                }
            }

            // normalize stop, correcting for negative indices and truncating to bounds
            if (!stop) {
                this->stop = neg ? -1 : size;  // neg: -1 | pos: size
            } else {
                this->stop = *stop;
                if (this->stop < 0) {
                    this->stop += size;
                    if (this->stop < 0) {
                        this->stop = -neg;  // neg: -1 | pos: 0
                    }
                } else if (this->stop >= size) {
                    this->stop = size - neg;  // neg: size - 1 | pos: size
                }
            }

            normalize(size);
        }

        void normalize(ssize_t size) noexcept {
            bool neg = step < 0;

            // compute # of elements => round((stop - start) / step) away from 0
            ssize_t delta = stop - start;
            ssize_t bias = step + (neg ? 1 : -1);
            length = (delta < 0) ^ neg ? size_t(0) : size_t((delta + bias) / step);
            if (length) {
                abs_step = neg ? -step : step;

                // convert from half-open [start, stop) to closed interval [start, stop]
                ssize_t mod = impl::pymod(delta, step);
                ssize_t closed = stop - (mod ? mod : step);
                backward = closer_to_tail(size, (start + closed + 1) / 2);

                // flip start/stop based on whether center of mass matches sign of step
                inverted = backward ^ neg;
                if (inverted) {
                    first = closed;
                    last = start;
                } else {
                    first = start;
                    last = closed;
                }
            }
        }
    };

    template <meta::unqualified View> requires (meta::linked_view<View>)
    struct slice_iterator {
    private:
        using view_iter = View::const_iterator;

        view_iter it;
        size_t idx = 0;
        size_t last = 0;
        ssize_t step = 0;
        bool reverse = false;

    public:
        using iterator_category = std::input_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = View::value_type;
        using reference = value_type&;
        using pointer = value_type*;

        slice_iterator() = default;

        slice_iterator(const View& view, normalize_slice indices, bool ok) {
            // if the slice is empty, return an empty iterator
            if (indices.length == 0) {
                return;
            }

            step = indices.abs_step;
            if (indices.backward) {
                // if ok, then the slice already has the correct logic.  Otherwise, we
                // need to reverse it.
                if (ok) {
                    it = {view.tail};
                    for (size_t i = view.size() - 1; i > indices.first; --i, --it);
                    idx = indices.first;
                    last = indices.last;
                    reverse = true;

                // if the last index is closer to the head of the list than the tail,
                // then we iterate from the head
                } else if (indices.last < (view.size() - 1 - indices.last)) {
                    it = {view.head};
                    for (size_t i = 0; i < indices.last; ++i, ++it);
                    idx = indices.last;
                    last = indices.first;
                    reverse = false;

                // otherwise, backtracking wins out, and we traverse the slice twice
                } else {
                    it = {view.tail};
                    for (size_t i = view.size() - 1; i > indices.last; --i, --it);
                    idx = indices.last;
                    last = indices.first;
                    reverse = false;
                }

            } else {
                // if ok, then the slice already has the correct logic.  Otherwise, we
                // need to reverse it.
                if (ok) {
                    it = {view.head};
                    for (size_t i = 0; i < indices.first; ++i, ++it);
                    idx = indices.first;
                    last = indices.last;
                    reverse = false;

                // if the last index is closer to the tail of the list than the head,
                // then we iterate from the tail
                } else if ((view.size() - 1 - indices.last) < indices.last) {
                    it = {view.tail};
                    for (size_t i = view.size() - 1; i > indices.last; --i, --it);
                    idx = indices.last;
                    last = indices.first;
                    reverse = true;

                // otherwise, backtracking wins out, and we traverse the slice twice
                } else {
                    it = {view.head};
                    for (size_t i = 0; i < indices.last; ++i, ++it);
                    idx = indices.last;
                    last = indices.first;
                    reverse = true;
                }
            }
        }

        [[nodiscard]] value_type& operator*() noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (*it == nullptr) {
                    throw MemoryError("cannot dereference a null iterator");
                }
            }
            return it->value;
        }

        [[nodiscard]] const value_type& operator*() const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (*it == nullptr) {
                    throw MemoryError("cannot dereference a null iterator");
                }
            }
            return it->value;
        }

        [[nodiscard]] value_type* operator->() noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (*it == nullptr) {
                    throw MemoryError("cannot dereference a null iterator");
                }
            }
            return &it->value;
        }

        [[nodiscard]] const value_type* operator->() const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (*it == nullptr) {
                    throw MemoryError("cannot dereference a null iterator");
                }
            }
            return &it->value;
        }

        slice_iterator& operator++() noexcept(!DEBUG) {
            if (reverse) {
                for (size_t i = 0; i < step; ++i) {
                    if constexpr (DEBUG) {
                        if (*it == nullptr) {
                            throw MemoryError("cannot dereference a null iterator");
                        }
                    }
                    --it;
                }
                idx -= step;
            } else {
                for (size_t i = 0; i < step; ++i) {
                    if constexpr (DEBUG) {
                        if (*it == nullptr) {
                            throw MemoryError("cannot dereference a null iterator");
                        }
                    }
                    ++it;
                }
                idx += step;
            }
            return *this;
        }

        [[nodiscard]] slice_iterator operator++(int) noexcept(!DEBUG) {
            slice_iterator temp = *this;
            ++(*this);
            return temp;
        }

        [[nodiscard]] friend bool operator==(
            const slice_iterator& self,
            sentinel
        ) noexcept {
            return self.reverse ? self.idx < self.last : self.idx > self.last;
        }

        [[nodiscard]] friend bool operator==(
            sentinel,
            const slice_iterator& self
        ) noexcept {
            return self.reverse ? self.idx < self.last : self.idx > self.last;
        }

        [[nodiscard]] friend bool operator!=(
            const slice_iterator& self,
            sentinel
        ) noexcept {
            return self.reverse ? self.idx >= self.last : self.idx <= self.last;
        }

        [[nodiscard]] friend bool operator!=(
            sentinel,
            const slice_iterator& self
        ) noexcept {
            return self.reverse ? self.idx >= self.last : self.idx <= self.last;
        }
    };

    template <meta::unqualified View> requires (meta::linked_view<View>)
    struct const_slice_iterator {
    private:
        using view_iter = View::const_iterator;

        view_iter it;
        size_t idx = 0;
        size_t last = 0;
        ssize_t step = 0;
        bool reverse = false;

    public:
        using iterator_category = std::input_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = const View::value_type;
        using reference = value_type&;
        using pointer = value_type*;

        const_slice_iterator() = default;

        const_slice_iterator(const View& view, normalize_slice indices, bool ok) {
            // if the slice is empty, return an empty iterator
            if (indices.length == 0) {
                return;
            }

            step = indices.abs_step;
            if (indices.backward) {
                // if ok, then the slice already has the correct logic.  Otherwise, we
                // need to reverse it.
                if (ok) {
                    it = {view.tail};
                    for (size_t i = view.size() - 1; i > indices.first; --i, --it);
                    idx = indices.first;
                    last = indices.last;
                    reverse = true;

                // if the last index is closer to the head of the list than the tail,
                // then we iterate from the head
                } else if (indices.last < (view.size() - 1 - indices.last)) {
                    it = {view.head};
                    for (size_t i = 0; i < indices.last; ++i, ++it);
                    idx = indices.last;
                    last = indices.first;
                    reverse = false;

                // otherwise, backtracking wins out, and we traverse the slice twice
                } else {
                    it = {view.tail};
                    for (size_t i = view.size() - 1; i > indices.last; --i, --it);
                    idx = indices.last;
                    last = indices.first;
                    reverse = false;
                }

            } else {
                // if ok, then the slice already has the correct logic.  Otherwise, we
                // need to reverse it.
                if (ok) {
                    it = {view.head};
                    for (size_t i = 0; i < indices.first; ++i, ++it);
                    idx = indices.first;
                    last = indices.last;
                    reverse = false;

                // if the last index is closer to the tail of the list than the head,
                // then we iterate from the tail
                } else if ((view.size() - 1 - indices.last) < indices.last) {
                    it = {view.tail};
                    for (size_t i = view.size() - 1; i > indices.last; --i, --it);
                    idx = indices.last;
                    last = indices.first;
                    reverse = true;

                // otherwise, backtracking wins out, and we traverse the slice twice
                } else {
                    it = {view.head};
                    for (size_t i = 0; i < indices.last; ++i, ++it);
                    idx = indices.last;
                    last = indices.first;
                    reverse = true;
                }
            }
        }

        [[nodiscard]] const value_type& operator*() const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (*it == nullptr) {
                    throw MemoryError("cannot dereference a null iterator");
                }
            }
            return it->value;
        }

        [[nodiscard]] const value_type* operator->() const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (*it == nullptr) {
                    throw MemoryError("cannot dereference a null iterator");
                }
            }
            return &it->value;
        }

        const_slice_iterator& operator++() noexcept(!DEBUG) {
            if (reverse) {
                for (size_t i = 0; i < step; ++i) {
                    if constexpr (DEBUG) {
                        if (*it == nullptr) {
                            throw MemoryError("cannot dereference a null iterator");
                        }
                    }
                    --it;
                }
                idx -= step;
            } else {
                for (size_t i = 0; i < step; ++i) {
                    if constexpr (DEBUG) {
                        if (*it == nullptr) {
                            throw MemoryError("cannot dereference a null iterator");
                        }
                    }
                    ++it;
                }
                idx += step;
            }
            return *this;
        }

        [[nodiscard]] const_slice_iterator operator++(int) noexcept(!DEBUG) {
            const_slice_iterator temp = *this;
            ++(*this);
            return temp;
        }

        [[nodiscard]] friend bool operator==(
            const const_slice_iterator& self,
            sentinel
        ) noexcept {
            return self.reverse ? self.idx < self.last : self.idx > self.last;
        }

        [[nodiscard]] friend bool operator==(
            sentinel,
            const const_slice_iterator& self
        ) noexcept {
            return self.reverse ? self.idx < self.last : self.idx > self.last;
        }

        [[nodiscard]] friend bool operator!=(
            const const_slice_iterator& self,
            sentinel
        ) noexcept {
            return self.reverse ? self.idx >= self.last : self.idx <= self.last;
        }

        [[nodiscard]] friend bool operator!=(
            sentinel,
            const const_slice_iterator& self
        ) noexcept {
            return self.reverse ? self.idx >= self.last : self.idx <= self.last;
        }
    };

    template <meta::unqualified View> requires (meta::linked_view<View>)
    struct const_slice : const_slice_tag {
    private:
        const View* view = nullptr;
        normalize_slice indices;

    public:
        using iterator = const_slice_iterator<View>;
        using const_iterator = const_slice_iterator<View>;

        const_slice() = default;

        const_slice(
            const View* view,
            const std::initializer_list<std::optional<ssize_t>>& indices
        ) :
            view(view),
            indices([](const View* view, const auto& indices) {
                if constexpr (DEBUG) {
                    if (view == nullptr) {
                        throw MemoryError("slice references a null view");
                    }
                }
                return normalize_slice(view->size(), indices);
            }(this->view, indices))
        {}

        const_slice(const const_slice&) = delete;
        const_slice& operator=(const const_slice&) = delete;

        const_slice(const_slice&&) = default;
        const_slice& operator=(const_slice&&) = default;

        /* The total number of elements included in the slice. */
        [[nodiscard]] size_t size() const noexcept { return indices.length; }

        /* True if the slice has zero size.  False otherwise. */
        [[nodiscard]] bool empty() const noexcept { return size() == 0; }

        /* True if the slice has non-zero size.  False otherwise. */
        [[nodiscard]] explicit operator bool() const noexcept { return !empty(); }

        /* The normalized start index that was given to the index operator. */
        [[nodiscard]] ssize_t start() const noexcept { return indices.start; }

        /* The normalized stop index that was given to the index operator. */
        [[nodiscard]] ssize_t stop() const noexcept { return indices.stop; }

        /* The normalized step size that was given to the index operator. */
        [[nodiscard]] ssize_t step() const noexcept { return indices.step; }

        /* Forward iterate over the slice contents as efficiently as possible. */
        [[nodiscard]] const_iterator begin() const noexcept {
            if constexpr (DEBUG) {
                if (view == nullptr) {
                    throw MemoryError("slice references a null view");
                }
            }
            return {*view, indices, !indices.inverted};
        }
        [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
        [[nodiscard]] sentinel end() const noexcept { return {}; }
        [[nodiscard]] sentinel cend() const noexcept { return {}; }

        /* Reverse iterate over the slice contents as efficiently as possible. */
        [[nodiscard]] const_iterator rbegin() const noexcept {
            if constexpr (DEBUG) {
                if (view == nullptr) {
                    throw MemoryError("slice references a null view");
                }
            }
            return {*view, indices, indices.inverted};
        }
        [[nodiscard]] const_iterator crbegin() const noexcept { return rbegin(); }
        [[nodiscard]] sentinel rend() const noexcept { return {}; }
        [[nodiscard]] sentinel crend() const noexcept { return {}; }

        /// TODO: implicit conversion to any linked data structure?  Also CTAD to
        /// automatically deduce it.  If iterating over the list in inverted order,
        /// I would just reverse the list again when constructing it, so that I never
        /// backtrack.

    };

    template <meta::unqualified View> requires (meta::linked_view<View>)
    struct slice : slice_tag {
    private:
        View* view;
        normalize_slice indices;

    public:
        using iterator = slice_iterator<View>;
        using const_iterator = const_slice_iterator<View>;

        slice() = default;

        slice(View* view, const std::initializer_list<std::optional<ssize_t>>& indices) :
            view(view),
            indices([](View* view, const auto& indices) {
                if constexpr (DEBUG) {
                    if (view == nullptr) {
                        throw MemoryError("slice references a null view");
                    }
                }
                return normalize_slice(view->size(), indices);
            }(this->view, indices))
        {}

        slice(const slice&) = delete;
        slice& operator=(const slice&) = delete;

        slice(slice&&) = default;
        slice& operator=(slice&&) = default;

        /* The total number of elements included in the slice. */
        [[nodiscard]] size_t size() const noexcept { return indices.length; }

        /* True if the slice has zero size.  False otherwise. */
        [[nodiscard]] bool empty() const noexcept { return size() == 0; }

        /* True if the slice has non-zero size.  False otherwise. */
        [[nodiscard]] explicit operator bool() const noexcept { return !empty(); }

        /* The normalized start index that was given to the index operator. */
        [[nodiscard]] ssize_t start() const noexcept { return indices.start; }

        /* The normalized stop index that was given to the index operator. */
        [[nodiscard]] ssize_t stop() const noexcept { return indices.stop; }

        /* The normalized step size that was given to the index operator. */
        [[nodiscard]] ssize_t step() const noexcept { return indices.step; }

        /* Forward iterate over the slice contents as efficiently as possible. */
        [[nodiscard]] iterator begin() noexcept {
            if constexpr (DEBUG) {
                if (view == nullptr) {
                    throw MemoryError("slice references a null view");
                }
            }
            return {*view, indices, !indices.inverted};
        }
        [[nodiscard]] const_iterator begin() const noexcept {
            if constexpr (DEBUG) {
                if (view == nullptr) {
                    throw MemoryError("slice references a null view");
                }
            }
            return {*view, indices, !indices.inverted};
        }
        [[nodiscard]] const_iterator cbegin() const noexcept { return begin(); }
        [[nodiscard]] sentinel end() noexcept { return {}; }
        [[nodiscard]] sentinel end() const noexcept { return {}; }
        [[nodiscard]] sentinel cend() const noexcept { return {}; }

        /* Reverse iterate over the slice contents as efficiently as possible. */
        [[nodiscard]] iterator rbegin() noexcept {
            if constexpr (DEBUG) {
                if (view == nullptr) {
                    throw MemoryError("slice references a null view");
                }
            }
            return {*view, indices, indices.inverted};
        }
        [[nodiscard]] const_iterator rbegin() const noexcept {
            if constexpr (DEBUG) {
                if (view == nullptr) {
                    throw MemoryError("slice references a null view");
                }
            }
            return {*view, indices, indices.inverted};
        }
        [[nodiscard]] const_iterator crbegin() const noexcept { return rbegin(); }
        [[nodiscard]] sentinel rend() noexcept { return {}; }
        [[nodiscard]] sentinel rend() const noexcept { return {}; }
        [[nodiscard]] sentinel crend() const noexcept { return {}; }

        /// TODO: inherit from const_slice and make it mutable by allowing it to be
        /// assigned to and passed to remove() + pop()

        /// TODO: allow implicit conversions, assignment and passing to pop() and
        /// remove().  Assignment should only be allowed for non-BST views, since the
        /// ordering is controlled externally.
    };

    template <typename Node>
    struct view_base : view_tag {};
    template <meta::bst_node Node>
    struct view_base<Node> : view_tag {
        Node* root = nullptr;
    };

    /// TODO: it may be necessary to constrain the copy/move constructors/assignment
    /// operators to only copy/move if the allocator and node values are
    /// copyable/movable, which also applies to the swap() method, etc.

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
        using node_type = Node;
        using allocator_type = Alloc;
        using size_type = size_t;
        using difference_type = std::ptrdiff_t;
        using value_type = Node::value_type;
        using reference = value_type&;
        using const_reference = const value_type&;
        using pointer = value_type*;
        using const_pointer = const value_type*;
        using iterator = node_iterator<node_type>;
        using const_iterator = node_iterator<const node_type>;
        using reverse_iterator = reverse_node_iterator<node_type>;
        using const_reverse_iterator = reverse_node_iterator<const node_type>;

        /* Indicates whether the data structure has a fixed capacity (true) or supports
        reallocations (false).  If true, then the value indicates the max capacity of
        the data structure. */
        static constexpr size_t STATIC = N;

        /* The minimum capacity for a dynamic array, to prevent thrashing.  This has no
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
            node->prev = nullptr;
            node->next = nullptr;
            if constexpr (meta::bst_node<Node>) {
                node->prev_thread = false;
                node->next_thread = false;
                node->red = false;
            }
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
            bool backward;
            size_t idx = normalize_index(array.size, i, backward);

            // if the index is closer to the tail of the list, start there.
            if (backward) {
                iterator it = {tail};
                ++idx;  // correct for zero indexing
                for (size_t j = array.size; j-- > idx;) {
                    --it;
                }
                return it;
            }

            // otherwise, start at the head
            iterator it {head};
            for (size_t j = 0; j++ < idx;) {
                ++it;
            }
            return it;
        }

        /* Return an iterator to the specified index of the list.  Allows Python-style
        negative indexing, and throws an `IndexError` if the index is out of bounds
        after normalization.  Has a time compexity of O(n/2) following the links
        between each node, starting from the nearest edge. */
        [[nodiscard]] const_iterator operator[](ssize_t i) const {
            bool backward;
            size_t idx = normalize_index(array.size, i);

            // if the index is closer to the tail of the list, start there.
            if (backward) {
                const_iterator it = {tail};
                ++idx;  // correct for zero indexing
                for (size_t j = array.size; j-- > idx;) {
                    --it;
                }
                return it;
            }

            // otherwise, start at the head
            const_iterator it {head};
            for (size_t j = 0; j++ < idx;) {
                ++it;
            }
            return it;
        }

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

        Array<N> array;
        Alloc allocator;

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
    };

    /* A wrapper around a node allocator for a linked set or map. */
    template <meta::unqualified Node, size_t N, meta::unqualified Alloc>
        requires (meta::hash_node<Node> && meta::allocator_for<Alloc, Node>)
    struct hash_view : view_base<Node> {
        /// TODO: reimplement the hopscotch hashing algorithm, which can also be
        /// composed with a BST representation for automatic sorting.


        /// TODO: create(...) may want to return an existing node one already exists
        /// in the table, in which case we would need a way to discriminate between
        /// fresh and existing nodes in the output of create().

        // /* Indicates whether the given node is reachable from this view.  This should
        // only be false for newly-allocated nodes returned from `create()`. */
        // template <meta::linked_view V>
        // [[nodiscard]] bool contains(Node* node) const noexcept(!DEBUG) {
        //     if constexpr (DEBUG) {
        //         if (node < array.data || node >= array.data + array.capacity) {
        //             throw MemoryError("node was not allocated from this view");
        //         }
        //     }
        //     return (node->prev || head == node) && (node->next || tail == node);
        // }
    };

    /// TODO: this list class could maybe be a base class for all linked data
    /// structures, which all expose things like size(), front(), back(), etc.
    /// -> rename to container<view>? So all the data structures need to do is
    /// figure out the correct view type, and then inherit the basic functionality
    /// and add any extras they might need.

    template <typename T, size_t N, typename Less, typename Alloc>
    struct list {
    protected:
        using node_type = node<T, void, Less>;

    public:
        using allocator_type = std::allocator_traits<Alloc>::template rebind_alloc<node_type>;
        using size_type = size_t;
        using difference_type = ptrdiff_t;
        using value_type = node_type::value_type;
        using reference = value_type&;
        using const_reference = const value_type&;
        using pointer = value_type*;
        using const_pointer = const value_type*;

    protected:
        using view_type = list_view<node_type, N, allocator_type>;

        view_type view;

    public:
        using iterator = value_iterator<typename view_type::iterator>;
        using const_iterator = value_iterator<typename view_type::const_iterator>;
        using reverse_iterator = value_iterator<typename view_type::reverse_iterator>;
        using const_reverse_iterator = value_iterator<typename view_type::const_reverse_iterator>;
        using slice = linked::slice<view_type>;
        using const_slice = linked::const_slice<view_type>;

        template <typename... Args> requires (std::constructible_from<view_type, Args...>)
        list(Args&&... args) noexcept(noexcept(view_type(std::forward<Args>(args)...))) :
            view(std::forward<Args>(args)...)
        {}

        /* Indicates whether the list has a fixed capacity (true) or supports
        reallocations (false).  If true, then the value indicates the capacity of the
        list. */
        static constexpr size_t STATIC = view_type::STATIC;

        /* The minimum capacity for the underlying array, to prevent thrashing.  This
        has no effect if the list has a fixed capacity (N > 0) */
        static constexpr size_t MIN_SIZE = view_type::MIN_SIZE;

        /* The number of elements in the list. */
        [[nodiscard]] size_type size() const noexcept(noexcept(view.size())) {
            return view.size();
        }

        /* True if the list has zero size.  False otherwise. */
        [[nodiscard]] bool empty() const noexcept(noexcept(view.empty())) {
            return view.empty();
        }

        /* True if the list has nonzero size.  False otherwise. */
        [[nodiscard]] explicit operator bool() const noexcept(noexcept(empty())) {
            return !empty();
        }

        /* The total number of elements the list can store before resizing. */
        [[nodiscard]] size_type capacity() const noexcept(noexcept(view.capacity())) {
            return view.capacity();
        }

        /* Estimate the overall memory usage of the list in bytes. */
        [[nodiscard]] size_t memory_usage() const noexcept(noexcept(view.memory_usage())) {
            return view.memory_usage();
        }

        /* Remove all elements from the list, resetting the size to zero, but leaving
        the capacity unchanged. */
        void clear() noexcept(noexcept(view.clear())) {
            view.clear();
        }

        /* A reference to the first element in the list. */
        [[nodiscard]] value_type& front() {
            if (view.head == nullptr) {
                throw IndexError("list is empty");
            }
            return view.head->value;
        }

        /* A reference to the first element in the list. */
        [[nodiscard]] const value_type& front() const {
            if (view.head == nullptr) {
                throw IndexError("list is empty");
            }
            return view.head->value;
        }

        /* A reference to the last element in the list. */
        [[nodiscard]] value_type& back() {
            if (view.tail == nullptr) {
                throw IndexError("list is empty");
            }
            return view.tail->value;
        }

        /* A reference to the last element in the list. */
        [[nodiscard]] const value_type& back() const {
            if (view.tail == nullptr) {
                throw IndexError("list is empty");
            }
            return view.tail->value;
        }

        [[nodiscard]] iterator begin() noexcept { return {view.head}; }
        [[nodiscard]] const_iterator begin() const noexcept { return {view.head}; }
        [[nodiscard]] const_iterator cbegin() const noexcept { return {view.head}; }
        [[nodiscard]] iterator end() noexcept { return {nullptr}; }
        [[nodiscard]] const_iterator end() const noexcept { return {nullptr}; }
        [[nodiscard]] const_iterator cend() const noexcept { return {nullptr}; }
        [[nodiscard]] reverse_iterator rbegin() noexcept { return {view.tail}; }
        [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return {view.tail}; }
        [[nodiscard]] const_reverse_iterator crbegin() const noexcept { return {view.tail}; }
        [[nodiscard]] reverse_iterator rend() noexcept { return {nullptr}; }
        [[nodiscard]] const_reverse_iterator rend() const noexcept { return {nullptr}; }
        [[nodiscard]] const_reverse_iterator crend() const noexcept { return {nullptr}; }

        /// TODO: maybe even the slice operators?
    };

    /// TODO: If I move to just a container<view> model, then dynamic_list can be
    /// generalized similarly to dynamic_container<view>.  The linked data structures
    /// would then deduce the correct view type and inherit from
    /// dynamic_container<view>, with all the extra logic held internally.

    template <typename T, size_t N, typename Less, typename Alloc>
    struct dynamic_list : list<T, N, Less, Alloc> {};
    template <typename T, typename Less, typename Alloc>
    struct dynamic_list<T, 0, Less, Alloc> : list<T, 0, Less, Alloc> {
        /* Increase the capacity of the list to store at least the given number of
        elements.  Not available for fixed-capacity lists (N > 0). */
        void reserve(size_t n) noexcept(noexcept(this->view.reserve(n))) {
            this->view.reserve(n);
        }

        /* Rearrange the nodes in memory to reflect their current list order, without
        changing the capacity.  This is done automatically whenever the underlying
        array grows or shrinks, and may be triggered manually as an optimization. */
        void defragment() noexcept(noexcept(this->view.defragment())) {
            this->view.defragment();
        }

        /* Shrink the capacity to the current size.  If the list is empty, then the
        underlying array will be deleted and the capacity set to zero.  Otherwise, if
        there are fewer than `MIN_SIZE` nodes, the capacity is set to `MIN_SIZE`
        instead.  Not available for fixed-capacity lists (N > 0). */
        void shrink() noexcept(noexcept(this->view.shrink())) {
            this->view.shrink();
        }
    };

    /* Factory function for converting a low-level view into a full-fledged data
    structure.  This uses a private constructor so as not to leak implementation
    details into the public interface. */
    template <meta::unqualified T, meta::linked_view View>
        requires (std::constructible_from<T, View>)
    [[nodiscard]] T make(View&& view) { return T(std::forward<View>(view)); }

}  // namespace impl::linked


namespace impl::linked {

    /// TODO: noexcept specifiers are hard to fully model in this case

    //////////////////////
    ////    APPEND    ////
    //////////////////////

    /// TODO: there might be problems using append() on hash tables, depending on how
    /// view.create() works in that case.  It probably just errors, so this should be
    /// basically fine?  Maybe I should suppress the error though, and just move the
    /// requested item to the back of the set/map?

    template <meta::linked_view View, typename... Args>
        requires (
            !meta::bst_view<View> &&
            !meta::hash_view<View> &&
            std::constructible_from<typename View::value_type, Args...>
        )
    void append(View& view, Args&&... args) {
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

    ///////////////////////
    ////    PREPEND    ////
    ///////////////////////

    template <meta::linked_view View, typename... Args>
        requires (
            !meta::bst_view<View> &&
            !meta::hash_view<View> &&
            std::constructible_from<typename View::value_type, Args...>
        )
    void prepend(View& view, Args&&... args) {
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

    //////////////////////
    ////    EXTEND    ////
    //////////////////////

    template <meta::linked_view View, meta::iterable Range>
        requires (
            !meta::bst_view<View> &&
            !meta::hash_view<View> &&
            std::convertible_to<meta::iter_type<Range>, typename View::value_type>
        )
    void extend(View& view, Range&& range) {
        auto it = std::ranges::begin(range);
        auto end = std::ranges::end(range);
        if (it == end) {
            return;
        }

        size_t orig_size = view.size();
        if constexpr (!View::STATIC && meta::has_size<Range>) {
            view.reserve(orig_size + std::ranges::size(range));
        }

        try {
            if (view.empty()) {
                view.head = view.create(*it);
                view.tail = view.head;
                ++it;
            }
            while (it != end) {
                view.tail->next = view.create(*it);
                view.tail->next->prev = view.tail;
                view.tail = view.tail->next;
                ++it;
            }

        } catch (...) {
            size_t diff = view.size() - orig_size;
            if (diff) {
                for (size_t i = 0; i < diff; ++i) {
                    auto* prev = view.tail->prev;
                    try {
                        view.recycle(view.tail);
                    } catch (...) {
                        if (prev) {
                            prev->next = nullptr;
                            view.tail = prev;
                        } else {
                            view.head = nullptr;
                        }
                        throw;
                    }
                    view.tail = prev;
                }
                if (view.tail) {
                    view.tail->next = nullptr;
                } else {
                    view.head = nullptr;
                }
            }
            throw;
        }
    }

    template <
        meta::linked_view View,
        std::input_or_output_iterator Begin,
        std::sentinel_for<Begin> End
    >
        requires (
            !meta::bst_view<View> &&
            !meta::hash_view<View> &&
            std::convertible_to<
                decltype(*std::declval<std::add_lvalue_reference_t<Begin>>()),
                typename View::value_type
            >
        )
    void extend(View& view, Begin&& it, End&& end) {
        if (it == end) {
            return;
        }

        size_t orig_size = view.size();
        if constexpr (!View::STATIC && meta::sub_returns<End, Begin, size_t>) {
            view.reserve(orig_size + size_t(end - it));
        }

        try {
            if (view.empty()) {
                view.head = view.create(*it);
                view.tail = view.head;
                ++it;
            }
            while (it != end) {
                view.tail->next = view.create(*it);
                view.tail->next->prev = view.tail;
                view.tail = view.tail->next;
                ++it;
            }

        } catch (...) {
            size_t diff = view.size() - orig_size;
            if (diff) {
                for (size_t i = 0; i < diff; ++i) {
                    auto* prev = view.tail->prev;
                    try {
                        view.recycle(view.tail);
                    } catch (...) {
                        if (prev) {
                            prev->next = nullptr;
                            view.tail = prev;
                        } else {
                            view.head = nullptr;
                        }
                        throw;
                    }
                    view.tail = prev;
                }
                if (view.tail) {
                    view.tail->next = nullptr;
                } else {
                    view.head = nullptr;
                }
            }
            throw;
        }
    }

    //////////////////////////
    ////    EXTENDLEFT    ////
    //////////////////////////

    template <meta::linked_view View, meta::iterable Range>
        requires (
            !meta::bst_view<View> &&
            !meta::hash_view<View> &&
            std::convertible_to<meta::iter_type<Range>, typename View::value_type>
        )
    void extendleft(View& view, const Range& range) {
        auto it = std::ranges::begin(range);
        auto end = std::ranges::end(range);
        if (it == end) {
            return;
        }

        size_t orig_size = view.size();
        if constexpr (!View::STATIC && meta::has_size<Range>) {
            view.reserve(orig_size + std::ranges::size(range));
        }

        try {
            if (view.empty()) {
                view.tail = view.create(*it);
                view.head = view.tail;
                ++it;
            }
            while (it != end) {
                view.head->prev = view.create(*it);
                view.head->prev->next = view.head;
                view.head = view.head->prev;
                ++it;
            }

        } catch (...) {
            size_t diff = view.size() - orig_size;
            if (diff) {
                for (size_t i = 0; i < diff; ++i) {
                    auto* next = view.head->next;
                    try {
                        view.recycle(view.head);
                    } catch (...) {
                        if (next) {
                            next->prev = nullptr;
                            view.head = next;
                        } else {
                            view.tail = nullptr;
                        }
                        throw;
                    }
                    view.head = next;
                }
                if (view.head) {
                    view.head->prev = nullptr;
                } else {
                    view.tail = nullptr;
                }
            }
            throw;
        }
    }

    template <
        meta::linked_view View,
        std::input_or_output_iterator Begin,
        std::sentinel_for<Begin> End
    >
        requires (
            !meta::bst_view<View> &&
            !meta::hash_view<View> &&
            std::convertible_to<
                decltype(*std::declval<std::add_lvalue_reference_t<Begin>>()),
                typename View::value_type
            >
        )
    void extendleft(View& view, Begin&& it, End&& end) {
        if (it == end) {
            return;
        }

        size_t orig_size = view.size();
        if constexpr (!View::STATIC && meta::sub_returns<End, Begin, size_t>) {
            view.reserve(orig_size + size_t(end - it));
        }

        try {
            if (view.empty()) {
                view.tail = view.create(*it);
                view.head = view.tail;
                ++it;
            }
            while (it != end) {
                view.head->prev = view.create(*it);
                view.head->prev->next = view.head;
                view.head = view.head->prev;
                ++it;
            }

        } catch (...) {
            size_t diff = view.size() - orig_size;
            if (diff) {
                for (size_t i = 0; i < diff; ++i) {
                    auto* next = view.head->next;
                    try {
                        view.recycle(view.head);
                    } catch (...) {
                        if (next) {
                            next->prev = nullptr;
                            view.head = next;
                        } else {
                            view.tail = nullptr;
                        }
                        throw;
                    }
                    view.head = next;
                }
                if (view.head) {
                    view.head->prev = nullptr;
                } else {
                    view.tail = nullptr;
                }
            }
            throw;
        }
    }

    ////////////////////////
    ////    CONTAINS    ////
    ////////////////////////

    template <meta::linked_view View, typename T>
        requires (
            !meta::bst_view<View> &&
            !meta::hash_view<View> &&
            meta::eq_returns<const typename View::value_type&, const T&, bool>
        )
    [[nodiscard]] bool contains(const View& view, const T& value) noexcept(
        noexcept(std::declval<const typename View::value_type&>() == value)
    ) {
        for (const auto& node : view) {
            if (node.value == value) {
                return true;
            }
        }
        return false;
    }

    // template <meta::bst_view View, typename T>
    //     requires (
    //         !meta::hash_view<View>
    //         /// TODO: confirm less_type is callable with the value_type
    //     )
    // [[nodiscard]] bool contains(View&& view, T&& value) {
    //     /// TODO: binary search
    // }

    // template <meta::hash_view View, typename T>
    //     requires (

    //     )
    // [[nodiscard]] bool contains(View&& view, T&& value) {

    // }

    /////////////////////
    ////    COUNT    ////
    /////////////////////

    template <meta::linked_view View, typename T>
        requires (
            !meta::bst_view<View> &&
            !meta::hash_view<View> &&
            meta::eq_returns<const typename View::value_type&, const T&, bool>
        )
    [[nodiscard]] size_t count(const View& view, const T& value) noexcept(
        noexcept(std::declval<const typename View::value_type&>() == value)
    ) {
        size_t count = 0;
        for (const auto& node : view) {
            count += node.value == value;
        }
        return count;
    }

    template <meta::linked_view View, typename T>
        requires (
            !meta::bst_view<View> &&
            !meta::hash_view<View> &&
            meta::eq_returns<const typename View::value_type&, const T&, bool>
        )
    [[nodiscard]] size_t count(
        const View& view,
        const T& value,
        std::optional<ssize_t> start,
        std::optional<ssize_t> stop
    ) noexcept(
        noexcept(normalize_slice(view.size(), start, stop)) &&
        noexcept(std::declval<const typename View::value_type&>() == value)
    ) {
        if (view.empty()) {
            return 0;
        }

        normalize_slice indices(view.size(), start, stop);
        if (indices.length == 0) {
            return 0;
        }

        size_t count = 0;
        if (indices.backward) {
            auto it = view.rbegin();
            size_t i = view.size();
            for (; i-- > indices.last; ++it);
            for (; i >= indices.first; --i, ++it) {
                count += it->value == value;
            }
        } else {
            auto it = view.begin();
            size_t i = 0;
            for (; i < indices.first; ++i, ++it);
            for (; i <= indices.last; ++i, ++it) {
                count += it->value == value;
            }
        }
        return count;
    }

    /////////////////////
    ////    INDEX    ////
    /////////////////////


    ///////////////////////
    ////    REVERSE    ////
    ///////////////////////

    template <meta::linked_view View> requires (!meta::bst_view<View>)
    void reverse(View& view) noexcept {
        auto* head = view.head;
        auto* curr = head;
        while (curr) {
            auto* next = curr->next;
            curr->next = curr->prev;
            curr->prev = next;
            curr = next;
        }
        view.head = view.tail;
        view.tail = head;
    }

    //////////////////////
    ////    ROTATE    ////
    //////////////////////

    template <meta::linked_view View> requires (!meta::bst_view<View>)
    void rotate(View& view, ssize_t n) noexcept {
        if (view.empty()) {
            return;
        }
        size_t steps = size_t(n < 0 ? -n : n) % view.size();
        if (steps == 0) {
            return;
        }

        size_t index;
        if (n < 0) {
            index = steps;
        } else {
            index = view.size() - steps;
        }

        if (closer_to_tail(view.size(), index)) {
            auto* new_head = view.tail;
            for (size_t i = view.size() - 1; i > index; --i) {
                new_head = new_head->prev;
            }
            auto* new_tail = new_head->prev;
            view.tail->next = view.head;
            view.head->prev = view.tail;
            view.tail = new_tail;
            view.head = new_head;
        } else {
            auto* new_tail = view.head;
            for (size_t i = 1; i < index; ++i) {
                new_tail = new_tail->next;
            }
            auto* new_head = new_tail->next;
            view.tail->next = view.head;
            view.head->prev = view.tail;
            view.tail = new_tail;
            view.head = new_head;
        }
    }

    //////////////////////
    ////    REMOVE    ////
    //////////////////////

    template <meta::linked_view View> requires (!meta::bst_view<View>)
    void remove_front(View& view) {
        if (view.empty()) {
            throw IndexError("list is empty");
        }
        auto* node = view.head;
        view.head = node->next;
        if (view.head) {
            view.head->prev = nullptr;
        } else {
            view.tail = nullptr;
        }
        view.recycle(node);
    }

    template <meta::linked_view View> requires (!meta::bst_view<View>)
    void remove_back(View& view) {
        if (view.empty()) {
            throw IndexError("list is empty");
        }
        auto* node = view.tail;
        view.tail = node->prev;
        if (view.tail) {
            view.tail->next = nullptr;
        } else {
            view.head = nullptr;
        }
        view.recycle(node);
    }

    /// TODO: remove() taking an iterator?  Maybe also a slice?

    ///////////////////
    ////    POP    ////
    ///////////////////

    template <meta::linked_view View> requires (!meta::bst_view<View>)
    auto pop_front(View& view) {
        if (view.empty()) {
            throw IndexError("list is empty");
        }
        auto* node = view.head;
        view.head = node->next;
        if (view.head) {
            view.head->prev = nullptr;
        } else {
            view.tail = nullptr;
        }
        auto value = std::move(node->value);
        view.recycle(node);
        return value;
    }

    template <meta::linked_view View> requires (!meta::bst_view<View>)
    auto pop_back(View& view) {
        if (view.empty()) {
            throw IndexError("list is empty");
        }
        auto* node = view.tail;
        view.tail = node->prev;
        if (view.tail) {
            view.tail->next = nullptr;
        } else {
            view.head = nullptr;
        }
        auto value = std::move(node->value);
        view.recycle(node);
        return value;
    }

    /// TODO: pop() taking an iterator?


}  // namespace impl::linked


/* A generic, doubly-linked list with optional ordering.

Unlike traditional linked lists, this implementation uses a single, contiguous array of
nodes to store the contents, similar to a `std::vector`.  This greatly increases cache
locality compared to a `std::list` or similar data structure, and yields overall
iteration performance comparable to a naked array, without sacrificing the fast
insertions and deletions that linked lists are known for.  It comes at the cost of
unconditional address stability in the dynamic case, since the node array may be
reallocated if the list grows larger than the current capacity.  However, that can be
fully mitigated by carefully controlling the growth of the list, or by setting a fixed
capacity at compile time, in which case no reallocations will ever occur, and the
addresses of the contents will remain stable over the lifetime of the list.

If `N` is set to zero (the default), the list array will be dynamically allocated using
the supplied (STL-compliant) allocator, and will grow as needed, mirroring the
semantics of `std::vector`.  Just like `std::vector`, growth occurs when `size()` is
equal to `capacity()` and a new item is added to the list, which triggers the
allocation of a new array and an O(n) move of the existing contents, invalidating any
pointers/iterators to the previous values.

If `N` is non-zero, then the list will be stack-allocated with a fixed capacity equal
to `N`, and will never grow or shrink beyond that capacity.  In fact, it will never
interact with the allocator at all except to `construct()` or `destroy()` nodes via
placement new or in-place destructor calls, respectively.  This recovers most of the
traditional address stability of a linked list (outside of explicit copies/moves/swaps)
while retaining the cache locality of a contiguous array, making such a list suitable
for use in cases where the heap is not available, such as in embedded systems or kernel
modules.

If a non-void `Less` type is provided, then the list will be trivially converted into a
binary search tree (BST) with the given ordering, similar to a `std::set`, but
allowing duplicate values.  `Less` is expected to be a default-constructible function
object whose call operator implements a less-than comparison between arbitrary values,
minimally including the contained type `T`.  This mirrors the same `Less` type used in
STL containers like `std::set`, except that `Less::is_transparent` is assumed to be
true by default in order to simplify downstream user code.  If you'd like to pass
through to the typical `<` operator for `T`, you can use `std::less<>` or
`std::less<void>`, just as you would with an STL container.

Binary search trees of this form are implemented using a threaded, top-down, red-black
balancing scheme, which requires no extra memory to store.  The necessary thread and
color bits are encoded directly into the `prev` and `next` pointers using pointer
tagging, allowing efficient in-order traversals without requiring a recursive stack or
`parent` pointer.  They support all of the same algorithms as an ordinary linked list,
except for those that modify the order of the nodes, such as `append()`, `sort()`,
`reverse()`, etc. */
template <
    typename T,
    size_t N = 0,
    typename Less = void,
    typename Alloc = std::allocator<T>
>
    requires (
        !std::is_reference_v<T> &&
        (meta::is_void<Less> || (
            std::is_default_constructible_v<Less> &&
            std::is_invocable_r_v<bool, Less, const T&, const T&>
        )) &&
        meta::allocator_for<Alloc, T>
    )
struct linked_list : impl::linked::dynamic_list<T, N, Less, Alloc> {
private:
    using base = impl::linked::dynamic_list<T, N, Less, Alloc>;

public:
    using allocator_type = base::allocator_type;
    using size_type = base::size_type;
    using difference_type = base::difference_type;
    using value_type = base::value_type;
    using reference = base::reference;
    using const_reference = base::const_reference;
    using pointer = base::pointer;
    using const_pointer = base::const_pointer;

protected:
    using View = base::View;

    View view;

public:
    using base::base;

    /// TODO: iterators, etc.

};


/* Specialization for pure, non-BST linked lists, without any strict ordering. */
template <typename T, size_t N, typename Alloc>
struct linked_list<T, N, void, Alloc> : impl::linked::dynamic_list<T, N, void, Alloc> {
private:
    using base = impl::linked::dynamic_list<T, N, void, Alloc>;

public:
    using allocator_type = base::allocator_type;
    using size_type = base::size_type;
    using difference_type = base::difference_type;
    using value_type = base::value_type;
    using reference = base::reference;
    using const_reference = base::const_reference;
    using pointer = base::pointer;
    using const_pointer = base::const_pointer;
    using iterator = base::iterator;
    using const_iterator = base::const_iterator;
    using reverse_iterator = base::reverse_iterator;
    using const_reverse_iterator = base::const_reverse_iterator;
    using slice = base::slice;
    using const_slice = base::const_slice;

    using base::STATIC;
    using base::MIN_SIZE;

protected:
    using base::view;

    template <meta::unqualified V, meta::linked_view View>
        requires (std::constructible_from<V, View>)
    friend V impl::linked::make(View&& view);

    template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End>
        requires (std::convertible_to<
            decltype(*std::declval<std::add_lvalue_reference_t<Begin>>()),
            value_type
        >)
    void construct(Begin& it, End& end) {
        view.head = view.create(*it);
        view.tail = view.head;
        ++it;
        while (it != end) {
            view.tail->next = view.create(*it);
            view.tail->next->prev = view.tail;
            view.tail = view.tail->next;
            ++it;
        }
    }

    template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End>
        requires (std::convertible_to<
            decltype(*std::declval<std::add_lvalue_reference_t<Begin>>()),
            value_type
        >)
    void construct(Begin& it, End& end, bool reverse) {
        view.head = view.create(*it);
        view.tail = view.head;
        ++it;
        if (reverse) {
            while (it != end) {
                view.head->prev = view.create(*it);
                view.head->prev->next = view.head;
                view.head = view.head->prev;
                ++it;
            }
        } else {
            while (it != end) {
                view.tail->next = view.create(*it);
                view.tail->next->prev = view.tail;
                view.tail = view.tail->next;
                ++it;
            }
        }
    }

    explicit linked_list(typename base::view_type&& view) : base(std::move(view)) {}
    explicit linked_list(const typename base::view_type& view) : base(view) {}

public:
    linked_list(allocator_type&& alloc = {}) : base(std::move(alloc)) {}
    linked_list(const allocator_type& alloc) : base(alloc) {}

    /* Construct a linked list from the contents of an iterable range.  Invokes
    implicit conversions to the contained type for each item.  If called with an
    extra boolean, the order of the range will be implicitly reversed, even if the
    incoming container does not support reverse iteration.  This is more efficient
    than constructing the list and then calling `list.reverse()` in-place. */
    template <meta::iterable Range>
        requires (std::convertible_to<meta::iter_type<Range>, value_type>)
    explicit linked_list(Range&& range, bool reverse = false, allocator_type&& alloc = {}) :
        base(std::move(alloc))
    {
        auto it = std::ranges::begin(range);
        auto end = std::ranges::end(range);
        if (it == end) {
            return;
        }
        if constexpr (!base::STATIC && meta::has_size<Range>) {
            view.reserve(std::ranges::size(range));
        }
        construct(it, end, reverse);
    }

    /* Construct a linked list from the contents of an iterable range.  Invokes
    implicit conversions to the contained type for each item.  If called with an
    extra boolean, the order of the range will be implicitly reversed, even if the
    incoming container does not support reverse iteration.  This is more efficient
    than constructing the list and then calling `list.reverse()` in-place. */
    template <meta::iterable Range>
        requires (std::convertible_to<meta::iter_type<Range>, value_type>)
    explicit linked_list(Range&& range, bool reverse, const allocator_type& alloc) :
        base(alloc)
    {
        auto it = std::ranges::begin(range);
        auto end = std::ranges::end(range);
        if (it == end) {
            return;
        }
        if constexpr (!base::STATIC && meta::has_size<Range>) {
            view.reserve(std::ranges::size(range));
        }
        construct(it, end, reverse);
    }

    /* Construct a linked list from the contents of an iterable range.  Invokes
    implicit conversions to the contained type for each item.  If called with an
    extra boolean, the order of the range will be implicitly reversed, even if the
    incoming iterators do not support reverse iteration.  This is more efficient
    than constructing the list and then calling `list.reverse()` in-place. */
    template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End>
        requires (std::convertible_to<
            decltype(*std::declval<std::add_lvalue_reference_t<Begin>>()),
            value_type
        >)
    linked_list(Begin&& it, End&& end, bool reverse = false, allocator_type&& alloc = {}) :
        base(std::move(alloc))
    {
        if (it == end) {
            return;
        }
        if constexpr (!base::STATIC && meta::sub_returns<End, Begin, size_t>) {
            view.reserve(size_t(end - it));
        }
        construct(it, end, reverse);
    }

    /* Construct a linked list from the contents of an iterable range.  Invokes
    implicit conversions to the contained type for each item.  If called with an
    extra boolean, the order of the range will be implicitly reversed, even if the
    incoming iterators do not support reverse iteration.  This is more efficient
    than constructing the list and then calling `list.reverse()` in-place. */
    template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End>
        requires (std::convertible_to<
            decltype(*std::declval<std::add_lvalue_reference_t<Begin>>()),
            value_type
        >)
    linked_list(Begin&& it, End&& end, bool reverse, const allocator_type& alloc) :
        base(alloc)
    {
        if (it == end) {
            return;
        }
        if constexpr (!base::STATIC && meta::sub_returns<End, Begin, size_t>) {
            view.reserve(size_t(end - it));
        }
        construct(it, end, reverse);
    }

    /* Construct a linked list from an explicit initializer.  Invokes implicit
    conversions to the contained type for each item. */
    linked_list(std::initializer_list<T> values, allocator_type&& alloc = {}) :
        base(std::move(alloc))
    {
        if (values.size() == 0) {
            return;
        }
        if constexpr (!base::STATIC) {
            view.reserve(values.size());
        }
        auto it = values.begin();
        auto end = values.end();
        construct(it, end);
    }

    /* Construct a linked list from an explicit initializer.  Invokes implicit
    conversions to the contained type for each item. */
    linked_list(std::initializer_list<T> values, const allocator_type& alloc) :
        base(alloc)
    {
        if (values.size() == 0) {
            return;
        }
        if constexpr (!base::STATIC) {
            view.reserve(values.size());
        }
        auto it = values.begin();
        auto end = values.end();
        construct(it, end);
    }

    /* Add an item to the end of the list.  Any arguments are passed directly to the
    constructor for `T`. */
    template <typename... Args> requires (std::constructible_from<value_type, Args...>)
    void append(Args&&... args) noexcept(
        noexcept(impl::linked::append(view, std::forward<Args>(args)...))
    ) {
        impl::linked::append(view, std::forward<Args>(args)...);
    }

    /* Add an item to the front of the list.  Any arguments are passed directly to the
    constructor for `T`. */
    template <typename... Args> requires (std::constructible_from<value_type, Args...>)
    void prepend(Args&&... args) noexcept(
        noexcept(impl::linked::prepend(view, std::forward<Args>(args)...))
    ) {
        impl::linked::prepend(view, std::forward<Args>(args)...);
    }

    /* Grow the list from the tail, appending all items from an iterable range.
    Invokes implicit conversions to the contained type for each item.  In the event of
    an error, the list will be rolled back to its original state. */
    template <meta::iterable Range>
        requires (std::convertible_to<meta::iter_type<Range>, value_type>)
    void extend(Range&& range) noexcept(
        noexcept(impl::linked::extend(view, std::forward<Range>(range)))
    ) {
        impl::linked::extend(view, std::forward<Range>(range));
    }

    /* Grow the list from the tail, appending all items from an iterable range.
    Invokes implicit conversions to the contained type for each item.  In the event of
    an error, the list will be rolled back to its original state. */
    template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End>
        requires (std::convertible_to<
            decltype(*std::declval<std::add_lvalue_reference_t<Begin>>()),
            value_type
        >)
    void extend(Begin&& it, End&& end) noexcept(
        noexcept(impl::linked::extend(view, std::forward<Begin>(it), std::forward<End>(end)))
    ) {
        impl::linked::extend(view, std::forward<Begin>(it), std::forward<End>(end));
    }

    /* Grow the list from the head, prepending all items from an iterable range.
    Invokes implicit conversions to the contained type for each item.  Note that due
    to the logic of `prepend()`, this method will implicitly reverse the contents of
    the range upon insertion.  In the event of an error, the list will be rolled back
    to its original state. */
    template <meta::iterable Range>
        requires (std::convertible_to<meta::iter_type<Range>, value_type>)
    void extendleft(Range&& range) noexcept(
        noexcept(impl::linked::extendleft(view, std::forward<Range>(range)))
    ) {
        impl::linked::extendleft(view, std::forward<Range>(range));
    }

    /* Grow the list from the head, prepending all items from an iterable range.
    Invokes implicit conversions to the contained type for each item.  Note that due
    to the logic of `prepend()`, this method will implicitly reverse the contents of
    the range upon insertion.  In the event of an error, the list will be rolled back
    to its original state. */
    template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End>
        requires (std::convertible_to<
            decltype(*std::declval<std::add_lvalue_reference_t<Begin>>()),
            value_type
        >)
    void extendleft(Begin&& it, End&& end) noexcept(
        noexcept(impl::linked::extendleft(view, std::forward<Begin>(it), std::forward<End>(end)))
    ) {
        impl::linked::extendleft(view, std::forward<Begin>(it), std::forward<End>(end));
    }

    /* Return true if the list contains the given value.  This performs a linear search
    through the list with an O(n) time complexity.  If the list is sorted, then this
    can be improved to O(log n) using a binary search. */
    template <typename V> requires (meta::eq_returns<const value_type&, const V&, bool>)
    [[nodiscard]] bool contains(const V& value) const noexcept(
        noexcept(impl::linked::contains(view, value))
    ) {
        return impl::linked::contains(view, value);
    }

    /* Count the number of occurrences of a given value in the list.  By default, this
    will loop over the whole list and compare against each value, but additional
    [start, stop) indices can be given to specify only a subrange, which will be
    scanned starting from the nearest end. */
    template <typename V> requires (meta::eq_returns<const value_type&, const V&, bool>)
    [[nodiscard]] size_t count(const V& value) const noexcept(
        noexcept(impl::linked::count(view, value))
    ) {
        return impl::linked::count(view, value);
    }

    /* Count the number of occurrences of a given value in the list.  By default, this
    will loop over the whole list and compare against each value, but additional
    [start, stop) indices can be given to specify only a subrange, which will be
    scanned starting from the nearest end.  The subrange indices may be negative or
    `std::nullopt`, following Python wraparound semantics. */
    template <typename V> requires (meta::eq_returns<const value_type&, const V&, bool>)
    [[nodiscard]] size_t count(
        const V& value,
        std::optional<ssize_t> start,
        std::optional<ssize_t> stop = std::nullopt
    ) const noexcept(noexcept(impl::linked::count(view, value, start, stop))) {
        return impl::linked::count(view, value, start, stop);
    }

    /* Reverse the order of the list's elements in-place. */
    void reverse() noexcept(noexcept(impl::linked::reverse(view))) {
        impl::linked::reverse(view);
    }

    /* Shift all elements in the list to the right by the specified number of steps.
    Negative values will shift to the left instead. */
    void rotate(ssize_t n = 1) noexcept(noexcept(impl::linked::rotate(view, n))) {
        impl::linked::rotate(view, n);
    }

    /// TODO: sort(), remove(), pop_back(), pop_front(), index(), operator*(), operator+()
    // index(), repr(), insert()

    /* Return an iterator to the specified index of the list.  Allows Python-style
    negative indexing, and throws an `IndexError` if the index is out of bounds
    after normalization.  Has a time compexity of O(n/2) following the links
    between each node, starting from the nearest edge. */
    [[nodiscard]] iterator operator[](ssize_t i) {
        return static_cast<iterator>(view[i]);
    }

    /* Return an iterator to the specified index of the list.  Allows Python-style
    negative indexing, and throws an `IndexError` if the index is out of bounds
    after normalization.  Has a time compexity of O(n/2) following the links
    between each node, starting from the nearest edge. */
    [[nodiscard]] const_iterator operator[](ssize_t i) const {
        return static_cast<const_iterator>(view[i]);
    }

    /* Return a range over a slice within the list, using the contents of a C++
    initializer list as Python-style start, stop, and step indices.  Each index can be
    `std::nullopt` (corresponding to `None` or an empty index in Python) or negative,
    applying the same wraparound as for scalar indexing.  Any out-of-bounds indices
    will be truncated to the nearest edge, possibly resulting in an empty slice.  Empty
    slices may also be returned if the start and stop indices do not conform to the
    given step size (e.g. if `start < stop`, but `step` is negative, or vice versa).

    A slice's natural iteration order is dictated by the step size, which defaults to
    1.  Forward iterating over it always yields values in that order, while reverse
    iterating will yield the same values, but in the opposite order.  This is distinct
    from using a negative step size, as the latter requires changes to the start/stop
    indices in order to yield the same contents following Python's half-open slice
    semantics.

    Slices can also be implicitly converted to a new list with arbitrary template
    parameters, as long as the underlying type is convertible.  If used, CTAD will
    always deduce to a dynamic list of the same type in this case, but the user can
    explicitly specify the template to convert to a fixed-size list of a different
    type, etc.  Conversely, assigning an iterable to the slice will trigger a traversal
    of the contents, assigning the values from the iterable to each element, provided
    the underlying element type supports it.  If a slice is passed to `list.remove()`
    or `list.pop()`, each element will be dropped from the list, modifying it in-place.
    If `pop()` is used, the previous values will be moved into a new list with the same
    configuration as the current list, which is returned to the caller. */
    [[nodiscard]] slice operator[](
        std::initializer_list<std::optional<ssize_t>> indices
    ) noexcept {
        return {view, indices};
    }

    /* Return a range over a slice within the list, using the contents of a C++
    initializer list as Python-style start, stop, and step indices.  Each index can be
    `std::nullopt` (corresponding to `None` or an empty index in Python) or negative,
    applying the same wraparound as for scalar indexing.  Any out-of-bounds indices
    will be truncated to the nearest edge, possibly resulting in an empty slice.  Empty
    slices may also be returned if the start and stop indices do not conform to the
    given step size (e.g. if `start < stop`, but `step` is negative, or vice versa).

    A slice's natural iteration order is dictated by the step size, which defaults to
    1.  Forward iterating over it always yields values in that order, while reverse
    iterating will yield the same values, but in the opposite order.  This is distinct
    from using a negative step size, as the latter requires changes to the start/stop
    indices in order to yield the same contents following Python's half-open slice
    semantics.

    Slices can also be implicitly converted to a new list with arbitrary template
    parameters, as long as the underlying type is convertible.  If used, CTAD will
    always deduce to a dynamic list of the same type in this case, but the user can
    explicitly specify the template to convert to a fixed-size list of a different
    type, etc.  Conversely, assigning an iterable to the slice will trigger a traversal
    of the contents, assigning the values from the iterable to each element, provided
    the underlying element type supports it.  If a slice is passed to `list.remove()`
    or `list.pop()`, each element will be dropped from the list, modifying it in-place.
    If `pop()` is used, the previous values will be moved into a new list with the same
    configuration as the current list, which is returned to the caller. */
    [[nodiscard]] const_slice operator[](
        std::initializer_list<std::optional<ssize_t>> indices
    ) const noexcept {
        return {view, indices};
    }

    /// TODO: repetition, concatenation

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


/// TODO: a non-member swap() function in the same namespace that swaps the resources
/// owned by two lists/sets/maps



}  // namespace bertrand


#endif  // BERTRAND_LINKED_LIST_H
