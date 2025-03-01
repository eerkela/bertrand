#ifndef BERTRAND_LIST_H
#define BERTRAND_LIST_H


#include "bertrand/common.h"
#include "bertrand/allocate.h"
#include "bertrand/except.h"
#include "bertrand/static_str.h"


namespace bertrand {


template <
    typename T,
    size_t N = impl::DEFAULT_ADDRESS_CAPACITY<T>,
    typename Equal = std::equal_to<>,
    typename Less = void
>
    requires (!meta::is_void<T> && (
        meta::is_void<Equal> || (
            std::is_default_constructible_v<Less> &&
            std::is_invocable_r_v<bool, Equal, const T&, const T&>
        )
    ) && (
        meta::is_void<Less> || (
            std::is_default_constructible_v<Less> &&
            std::is_invocable_r_v<bool, Less, const T&, const T&>
        )
    ))
struct List;


namespace impl {
    struct list_tag {};

    /* Immutable, bounds-checked iterator class for `bertrand::List`. */
    template <typename T> requires (!meta::reference<T> && !meta::is_void<T>)
    struct const_list_iterator {
        using iterator_category = std::contiguous_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = const T;
        using pointer = const value_type*;
        using reference = const value_type&;

    private:
        template <typename T2, size_t N>
        friend struct bertrand::List;

        const T* m_data = nullptr;
        size_t m_index = 0;
        size_t m_size = 0;

        const_list_iterator(const T* data, size_t index, size_t size) noexcept :
            m_data(data), m_index(index), m_size(size)
        {}

    public:
        const_list_iterator() = default;

        [[nodiscard]] const value_type& operator*() const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (m_index >= m_size) {
                    throw IndexError(std::to_string(static_cast<ssize_t>(m_index)));
                }
            }
            return m_data[m_index];
        }

        [[nodiscard]] const value_type* operator->() const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (m_index >= m_size) {
                    throw IndexError(std::to_string(static_cast<ssize_t>(m_index)));
                }
            }
            return &m_data[m_index];
        }

        [[nodiscard]] const value_type& operator[](difference_type n) const noexcept(!DEBUG) {
            size_t idx = m_index;
            if (n < 0) {
                idx -= static_cast<size_t>(-n);
            } else {
                idx += static_cast<size_t>(n);
            }
            if constexpr (DEBUG) {
                if (idx >= m_size) {
                    throw IndexError(std::to_string(static_cast<ssize_t>(idx)));
                }
            }
            return m_data[idx];
        }

        [[maybe_unused]] const_list_iterator& operator++() noexcept {
            ++m_index;
            return *this;
        }

        [[nodiscard]] const_list_iterator operator++(int) noexcept {
            const_list_iterator copy = *this;
            ++(*this);
            return copy;
        }

        [[maybe_unused]] const_list_iterator& operator+=(difference_type n) noexcept {
            m_index += n;
            return *this;
        }

        [[nodiscard]] const_list_iterator operator+(difference_type n) const noexcept {
            return {m_data, m_index + n, m_size};
        }

        [[maybe_unused]] const_list_iterator& operator--() noexcept {
            --m_index;
            return *this;
        }

        [[nodiscard]] const_list_iterator operator--(int) noexcept {
            const_list_iterator copy = *this;
            --(*this);
            return copy;
        }

        [[maybe_unused]] const_list_iterator& operator-=(difference_type n) noexcept {
            m_index -= n;
            return *this;
        }

        [[nodiscard]] const_list_iterator operator-(difference_type n) const noexcept {
            return {m_data, m_index - n, m_size};
        }

        [[nodiscard]] difference_type operator-(
            const const_list_iterator& other
        ) const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (m_data != other.m_data) {
                    throw ValueError(
                        "cannot compare iterators from different lists"
                    );
                }
            }
            return m_index - other.m_index;
        }

        [[nodiscard]] friend constexpr bool operator<(
            const const_list_iterator& lhs,
            const const_list_iterator& rhs
        ) noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (lhs.m_data != rhs.m_data) {
                    throw ValueError(
                        "cannot compare iterators from different lists"
                    );
                }
            }
            return lhs.m_index < rhs.m_index;
        }

        [[nodiscard]] friend constexpr bool operator<=(
            const const_list_iterator& lhs,
            const const_list_iterator& rhs
        ) noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (lhs.m_data != rhs.m_data) {
                    throw ValueError(
                        "cannot compare iterators from different lists"
                    );
                }
            }
            return lhs.m_index <= rhs.m_index;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const const_list_iterator& lhs,
            const const_list_iterator& rhs
        ) noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (lhs.m_data != rhs.m_data) {
                    throw ValueError(
                        "cannot compare iterators from different lists"
                    );
                }
            }
            return lhs.m_index == rhs.m_index;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            const const_list_iterator& lhs,
            const const_list_iterator& rhs
        ) noexcept(!DEBUG) {
            return !(lhs == rhs);
        }

        [[nodiscard]] friend constexpr bool operator>=(
            const const_list_iterator& lhs,
            const const_list_iterator& rhs
        ) noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (lhs.m_data != rhs.m_data) {
                    throw ValueError(
                        "cannot compare iterators from different lists"
                    );
                }
            }
            return lhs.m_index >= rhs.m_index;
        }

        [[nodiscard]] friend constexpr bool operator>(
            const const_list_iterator& lhs,
            const const_list_iterator& rhs
        ) noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (lhs.m_data != rhs.m_data) {
                    throw ValueError(
                        "cannot compare iterators from different lists"
                    );
                }
            }
            return lhs.m_index > rhs.m_index;
        }

        template <typename V> requires (std::convertible_to<const value_type&, V>)
        [[nodiscard]] operator V() const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (m_index >= m_size) {
                    throw IndexError(std::to_string(static_cast<ssize_t>(m_index)));
                }
            }
            return m_data[m_index];
        }
    };

    /* Mutable, bounds-checked iterator class for `bertrand::List`. */
    template <typename T> requires (!meta::reference<T> && !meta::is_void<T>)
    struct list_iterator {
        using iterator_category = std::contiguous_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using pointer = value_type*;
        using reference = value_type&;

    private:
        template <typename T2, size_t N>
        friend struct bertrand::List;

        T* m_data = nullptr;
        size_t m_index = 0;
        size_t m_size = 0;

        list_iterator(T* data, size_t index, size_t size) noexcept :
            m_data(data), m_index(index), m_size(size)
        {}

    public:
        list_iterator() = default;
        list_iterator(const list_iterator&) = default;
        list_iterator(list_iterator&&) = default;
        list_iterator& operator=(const list_iterator&) = default;
        list_iterator& operator=(list_iterator&&) = default;

        [[nodiscard]] value_type& operator*() noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (m_index >= m_size) {
                    throw IndexError(std::to_string(static_cast<ssize_t>(m_index)));
                }
            }
            return m_data[m_index];
        }

        [[nodiscard]] const value_type& operator*() const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (m_index >= m_size) {
                    throw IndexError(std::to_string(static_cast<ssize_t>(m_index)));
                }
            }
            return m_data[m_index];
        }

        [[nodiscard]] value_type* operator->() noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (m_index >= m_size) {
                    throw IndexError(std::to_string(static_cast<ssize_t>(m_index)));
                }
            }
            return &m_data[m_index];
        }

        [[nodiscard]] const value_type* operator->() const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (m_index >= m_size) {
                    throw IndexError(std::to_string(static_cast<ssize_t>(m_index)));
                }
            }
            return &m_data[m_index];
        }

        [[nodiscard]] value_type& operator[](difference_type n) noexcept(!DEBUG) {
            size_t idx = m_index;
            if (n < 0) {
                idx -= static_cast<size_t>(-n);
            } else {
                idx += static_cast<size_t>(n);
            }
            if constexpr (DEBUG) {
                if (idx >= m_size) {
                    throw IndexError(std::to_string(static_cast<ssize_t>(idx)));
                }
            }
            return m_data[idx];
        }

        [[nodiscard]] const value_type& operator[](difference_type n) const noexcept(!DEBUG) {
            size_t idx = m_index;
            if (n < 0) {
                idx -= static_cast<size_t>(-n);
            } else {
                idx += static_cast<size_t>(n);
            }
            if constexpr (DEBUG) {
                if (idx >= m_size) {
                    throw IndexError(std::to_string(static_cast<ssize_t>(idx)));
                }
            }
            return m_data[idx];
        }

        [[maybe_unused]] list_iterator& operator++() noexcept {
            ++m_index;
            return *this;
        }

        [[nodiscard]] list_iterator operator++(int) noexcept {
            list_iterator copy = *this;
            ++(*this);
            return copy;
        }

        [[maybe_unused]] list_iterator& operator+=(difference_type n) noexcept {
            m_index += n;
            return *this;
        }

        [[nodiscard]] list_iterator operator+(difference_type n) const noexcept {
            return {m_data, m_index + n, m_size};
        }

        [[maybe_unused]] list_iterator& operator--() noexcept {
            --m_index;
            return *this;
        }

        [[nodiscard]] list_iterator operator--(int) noexcept {
            list_iterator copy = *this;
            --(*this);
            return copy;
        }

        [[maybe_unused]] list_iterator& operator-=(difference_type n) noexcept {
            m_index -= n;
            return *this;
        }

        [[nodiscard]] list_iterator operator-(difference_type n) const noexcept {
            return {m_data, m_index - n, m_size};
        }

        [[nodiscard]] difference_type operator-(
            const list_iterator& other
        ) const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (m_data != other.m_data) {
                    throw ValueError(
                        "cannot compare iterators from different lists"
                    );
                }
            }
            return m_index - other.m_index;
        }

        [[nodiscard]] friend constexpr bool operator<(
            const list_iterator& lhs,
            const list_iterator& rhs
        ) noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (lhs.m_data != rhs.m_data) {
                    throw ValueError(
                        "cannot compare iterators from different lists"
                    );
                }
            }
            return lhs.m_index < rhs.m_index;
        }

        [[nodiscard]] friend constexpr bool operator<=(
            const list_iterator& lhs,
            const list_iterator& rhs
        ) noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (lhs.m_data != rhs.m_data) {
                    throw ValueError(
                        "cannot compare iterators from different lists"
                    );
                }
            }
            return lhs.m_index <= rhs.m_index;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const list_iterator& lhs,
            const list_iterator& rhs
        ) noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (lhs.m_data != rhs.m_data) {
                    throw ValueError(
                        "cannot compare iterators from different lists"
                    );
                }
            }
            return lhs.m_index == rhs.m_index;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            const list_iterator& lhs,
            const list_iterator& rhs
        ) noexcept(!DEBUG) {
            return !(lhs == rhs);
        }

        [[nodiscard]] friend constexpr bool operator>=(
            const list_iterator& lhs,
            const list_iterator& rhs
        ) noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (lhs.m_data != rhs.m_data) {
                    throw ValueError(
                        "cannot compare iterators from different lists"
                    );
                }
            }
            return lhs.m_index >= rhs.m_index;
        }

        [[nodiscard]] friend constexpr bool operator>(
            const list_iterator& lhs,
            const list_iterator& rhs
        ) noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (lhs.m_data != rhs.m_data) {
                    throw ValueError(
                        "cannot compare iterators from different lists"
                    );
                }
            }
            return lhs.m_index > rhs.m_index;
        }

        template <typename V> requires (std::convertible_to<value_type&, V>)
        [[nodiscard]] operator V() noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (m_index >= m_size) {
                    throw IndexError(std::to_string(static_cast<ssize_t>(m_index)));
                }
            }
            return m_data[m_index];
        }

        template <typename V> requires (std::convertible_to<const value_type&, V>)
        [[nodiscard]] operator V() const noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (m_index >= m_size) {
                    throw IndexError(std::to_string(static_cast<ssize_t>(m_index)));
                }
            }
            return m_data[m_index];
        }

        template <typename V> requires (std::assignable_from<value_type&, V>)
        [[maybe_unused]] list_iterator& operator=(V&& value) && noexcept(!DEBUG) {
            if constexpr (DEBUG) {
                if (m_index >= m_size) {
                    throw IndexError(std::to_string(static_cast<ssize_t>(m_index)));
                }
            }
            m_data[m_index] = std::forward<V>(value);
            return *this;
        }
    };

    /* Immutable, bounds-checked slice class for `bertrand::List`. */
    template <typename T> requires (!meta::reference<T> && !meta::is_void<T>)
    struct const_list_slice {
    private:

        /// TODO: data, start, stop, step

    public:

    };

    /* Mutable, bounds-checked slice class for `bertrand::List`. */
    template <typename T> requires (!meta::reference<T> && !meta::is_void<T>)
    struct list_slice {

    };

}


namespace meta {

    template <typename T>
    concept List = inherits<T, impl::list_tag>;

    namespace detail {

        template <typename Less, List T>
            requires (
                std::is_default_constructible_v<Less> &&
                std::is_invocable_r_v<
                    bool,
                    Less,
                    const typename std::remove_cvref_t<T>::value_type&,
                    const typename std::remove_cvref_t<T>::value_type&    
                >
            )
        struct sorted<Less, T> {
            using type = std::remove_cvref_t<T>::template to_sorted<Less>;
        };

    }

}


/* A simple dynamic array that stores its contents in a contiguous virtual address
space, which can grow without relocation.

Typical dynamic array implementations (such as `std::vector<T>`) store their contents
on the heap, only allocating as much memory as they need, plus some excess to prevent
thrashing.  Once the available space is exhausted, the array must allocate a new block
of memory from the heap, move the contents of the old block to the new block, and then
free the old block.  This is a costly operation that leads to pointer invalidation for
the existing contents (a potential security risk) and unpredictable performance, with
frequent small stutters as the array grows, particularly impacting the memory
subsystem.  This can be mitigated to some extent by proper use of the `reserve()`
method, but that is not always feasible, and `reserve()` can still trigger relocation
if the array lacks sufficient capacity.

This class avoids dynamic allocations entirely by giving each list its own private
arena, which consists of a virtual address space that is decoupled from physical
memory.  Upon construction, each list reserves a range of virtual addresses (pointers)
without any physical memory to back them, and then directs the operating system to
allocate pages on-demand as they are needed.  Because of this, and because the
available virtual address space is quite large on 64-bit systems (on the order of
hundreds of terabytes), it is possible to reserve even very large contiguous address
spaces in an efficient manner







Such arrays exhibit strong pointer stability
and performance characteristics similar to typical STL containers, 





If virtual addresses are not enabled at
build time, then the list will fall back to traditional heap allocation using
`malloc()` and `free()`.

Virtual address spaces allow dynamic lists to be allocated within a reserved (usually
large) range of addresses, with physical memory being assigned on-demand when needed.
This allows the array to grow without relocating, up to the maximum size of the address
space (defaulting to ~32 MiB per list, which can be overridden).  If the address space
is exhausted (unlikely), then the allocator will throw a `MemoryError`, indicating that
the initial size should be increased.  Note that virtual address spaces require a
physical Memory Management Unit (MMU) to be present on the system, which is not
guaranteed on all platforms.  If the MMU is not present, or the operating system is
compiled without virtual memory support for some reason, then attempting to construct
a virtual address space will throw a `MemoryError`.  This can be avoided by checking
the status of `bertrand::HAS_ADDRESS_SPACE` and implementing a fallback path if
necessary.  Generally, almost all modern systems running either unix or Windows will
have an MMU as standard, so this is not a concern for most users.

Static (physical) spaces are similar to virtual spaces, except that they allocate all
of the required memory upfront as a raw array similar to `std::inplace_vector`, and
cannot grow beyond the initial size.  This is useful for small arrays that are known
not to grow past a certain size, and avoids the overhead of virtual memory
management/page allocation.  This also makes such lists suitable for use at compile
time.  Care should be taken to keep such spaces relatively small, and be generally
wary of placing them on the stack, as large spaces can easily lead to buffer overflows
and other security issues.  If this becomes a problem, then moving the physical space
to the heap or nesting it within a virtual address space can help mitigate the issue.

If the allocator is a standard STL allocator (such as `std::allocator<T>`), then the
list will be allocated using it, and will behave like a normal `std::vector<T>`.  This
is generally the least efficient option, as it requires frequent relocation of the
underlying data, leads to possible pointer invalidation, and causes inconsistent
insertion performance.  If dynamic allocation is required, then it is always
recommended to use an overcommitted virtual address space if possible, which largely
mitigates these issues. */
template <typename T, size_t N, typename Equal, typename Less>
    requires (!meta::is_void<T> && (
        meta::is_void<Equal> || (
            std::is_default_constructible_v<Less> &&
            std::is_invocable_r_v<bool, Equal, const T&, const T&>
        )
    ) && (
        meta::is_void<Less> || (
            std::is_default_constructible_v<Less> &&
            std::is_invocable_r_v<bool, Less, const T&, const T&>
        )
    ))
struct List : impl::list_tag {
    using size_type = size_t;
    using index_type = ssize_t;
    using difference_type = std::ptrdiff_t;
    using value_type = T;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using iterator = impl::list_iterator<T>;
    using const_iterator = impl::const_list_iterator<T>;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    /* Replace the `Less` type that this list was specialized with.  This is invoked
    when evaluating the `sorted()` operator on a `bertrand::List` container. */
    template <typename L>
        requires (meta::is_void<Less> || (
            std::is_default_constructible_v<Less> &&
            std::is_invocable_r_v<bool, Less, const T&, const L&>
        ))
    using to_sorted = List<T, N, Equal, L>;

    /* The default capacity to reserve if no template override is given.  This defaults
    to a maximum of ~8 MiB of total storage, evenly divided into contiguous segments of
    type `T`. */
    static constexpr bool DEFAULT_CAPACITY = impl::DEFAULT_ADDRESS_CAPACITY<T>;

    /* True if the container is backed by a stable address space, meaning that dynamic
    growth will not invalidate pointers to existing elements.  This is equivalent to
    the `bertrand::HAS_ARENA` flag */
    static constexpr bool STABLE_ADDRESS = HAS_ARENA;

    /* True if the container is naturally sorted, which occurs when it is templated
    with a non-void `Less` function.  Such a container is guaranteed to always maintain
    strict ascending order based on the comparison function, allowing for O(log(n))
    binary searches using any key that can be transparently evaluated by the `Less`
    function (as if the STL's `is_transparent` property were always true). */
    static constexpr bool SORTED = !meta::is_void<Less>;

private:
    template <typename V>
    static constexpr V& lvalue(V&& x) noexcept { return x; }

    constexpr size_type normalize_index(index_type i) {
        if (i < 0) {
            i += size();
            if (i < 0) {
                throw IndexError(std::to_string(i));
            }
        } else if (i >= size()) {
            throw IndexError(std::to_string(i));
        }
        return static_cast<size_type>(i);
    }

    enum Allocator {
        PHYSICAL,
        VIRTUAL,
        HEAP
    };

    template <Allocator flag>
    struct Alloc {
        size_type capacity = 0;
        size_type size = 0;
        address_space<T, N> space;

        constexpr pointer data() noexcept(noexcept(space.data())) {
            return space.data();
        }

        constexpr const_pointer data() const noexcept(noexcept(space.data())) {
            return space.data();
        }

        template <typename... Args>
        constexpr void construct(pointer p, Args&&... args) noexcept(
            noexcept(space.construct(p, std::forward<Args>(args)...))
        ) {
            space.construct(p, std::forward<Args>(args)...);
            ++size;
        }

        constexpr void destroy(pointer p) noexcept(
            noexcept(space.destroy(p))
        ) {
            space.destroy(p);
            --size;
        }

        constexpr void grow(size_type n) noexcept(
            noexcept(space.allocate(capacity, n - capacity))
        ) {
            space.allocate(capacity, n - capacity);
            capacity = n;
        }

        constexpr void shrink(size_type n) noexcept(
            noexcept(space.deallocate(n, capacity - n))
        ) {
            space.deallocate(n, capacity - n);
            capacity = n;
        }

        constexpr void clear() noexcept(
            std::is_trivially_destructible_v<T> ||
            noexcept(space.destroy(space.data()))
        ) {
            if constexpr (std::is_trivially_destructible_v<T>) {
                size = 0;
            } else {
                while (size) {
                    --size;
                    space.destroy(space.data() + size);
                }
            }
        }

        constexpr Alloc() = default;

        constexpr Alloc(const Alloc& other) noexcept(
            noexcept(grow(other.size)) &&
            noexcept(construct(data(), *other.data()))
        ) {
            if (other.size) {
                grow(other.size);
                while (size < other.size) {
                    try {
                        construct(data() + size, other.data()[size]);
                    } catch (...) {
                        clear();
                        throw;
                    }
                }
            }
        }

        constexpr Alloc(Alloc&& other) noexcept(
            noexcept(address_space<T, N>{}) &&
            noexcept(grow(other.size)) &&
            noexcept(space.construct(data(), std::move(*other.data()))) &&
            noexcept(other.destroy(other.data()))
        ) {
            if (other.size()) {
                grow(other.size);

                // if a move constructor or destructor fails, make a best-faith effort
                // to roll the previous contents back to the original container
                while (size < other.size) {
                    try {
                        space.construct(
                            data() + size,
                            std::move(other.data()[size])
                        );
                        other.destroy(other.data() + (size++));
                    } catch (...) {
                        while (size) {
                            --size;
                            other.construct(
                                other.data() + size,
                                std::move(data()[size])
                            );
                            space.destroy(data() + size);
                        }
                        throw;
                    }
                }
            }
        }

        constexpr Alloc& operator=(const Alloc& other) noexcept(
            noexcept(clear()) &&
            noexcept(grow(other.size)) &&
            noexcept(construct(data(), *other.space.data()))
        ) {
            if (this != &other) {
                clear();
                if (other.size > capacity) {
                    grow(other.size);
                }
                while (size < other.size) {
                    try {
                        construct(data() + size, other.data()[size]);
                    } catch (...) {
                        clear();
                        throw;
                    }
                }
            }
            return *this;
        }

        constexpr Alloc& operator=(Alloc&& other) noexcept(
            noexcept(clear()) &&
            noexcept(grow(other.size)) &&
            noexcept(space.construct(data(), std::move(*other.data()))) &&
            noexcept(other.destroy(other.data()))
        ) {
            if (this != &other) {
                clear();
                if (other.size > capacity) {
                    grow(other.size);
                }

                // if a move constructor or destructor fails, make a best-faith effort
                // to roll the previous contents back to the original container
                while (size < other.size) {
                    try {
                        space.construct(
                            data() + size,
                            std::move(other.data()[size])
                        );
                        other.destroy(other.data() + (size++));
                    } catch (...) {
                        while (size) {
                            --size;
                            other.construct(
                                other.data() + size,
                                std::move(data()[size])
                            );
                            space.destroy(data() + size);
                        }
                        throw;
                    }
                }
            }
            return *this;
        }

        constexpr ~Alloc() noexcept(
            noexcept(clear()) &&
            std::is_nothrow_destructible_v<address_space<T, N>>
        ) {
            clear();
        }
    };

    template <>
    struct Alloc<VIRTUAL> {
        size_type capacity = 0;
        size_type size = 0;
        address_space<T, N> space;

        constexpr pointer data() noexcept(noexcept(space.data())) {
            return space.data();
        }

        constexpr const_pointer data() const noexcept(noexcept(space.data())) {
            return space.data();
        }

        template <typename... Args>
        constexpr void construct(pointer p, Args&&... args) noexcept(
            noexcept(space.construct(p, std::forward<Args>(args)...))
        ) {
            space.construct(p, std::forward<Args>(args)...);
            ++size;
        }

        constexpr void destroy(pointer p) noexcept(
            noexcept(space.destroy(p))
        ) {
            space.destroy(p);
            --size;
        }

        constexpr void grow(size_type n) noexcept(
            noexcept(space.allocate(capacity, n - capacity))
        ) {
            space.allocate(capacity, n - capacity);
            capacity = n;
        }

        constexpr void shrink(size_type n) noexcept(
            noexcept(space.deallocate(n, capacity - n))
        ) {
            space.deallocate(n, capacity - n);
            capacity = n;
        }

        constexpr void clear() noexcept(
            std::is_trivially_destructible_v<T> ||
            noexcept(space.destroy(space.data()))
        ) {
            if constexpr (std::is_trivially_destructible_v<T>) {
                size = 0;
            } else {
                while (size) {
                    --size;
                    space.destroy(space.data() + size);
                }
            }
        }

        constexpr Alloc() = default;

        constexpr Alloc(const Alloc& other) noexcept(
            noexcept(grow(other.size)) &&
            noexcept(construct(data(), *other.data()))
        ) {
            if (other.size) {
                grow(other.size);
                while (size < other.size) {
                    try {
                        construct(data() + size, other.data()[size]);
                    } catch (...) {
                        clear();
                        throw;
                    }
                }
            }
        }

        constexpr Alloc(Alloc&& other) noexcept(
            noexcept(address_space<T, N>(std::move(other.space)))
        ) :
            capacity(other.capacity),
            size(other.size),
            space(std::move(other.space))
        {}

        constexpr Alloc& operator=(const Alloc& other) noexcept(
            noexcept(clear()) &&
            noexcept(grow(other.size)) &&
            noexcept(construct(data(), *other.space.data()))
        ) {
            if (this != &other) {
                clear();
                if (other.size > capacity) {
                    grow(other.size);
                }
                while (size < other.size) {
                    try {
                        construct(data() + size, other.data()[size]);
                    } catch (...) {
                        clear();
                        throw;
                    }
                }
            }
            return *this;
        }

        constexpr Alloc& operator=(Alloc&& other) noexcept(
            noexcept(clear()) &&
            noexcept(space = std::move(other.space))
        ) {
            if (this != &other) {
                clear();
                capacity = other.capacity;
                size = other.size;
                space = std::move(other.space);
                other.capacity = 0;
                other.size = 0;
            }
            return *this;
        }

        constexpr ~Alloc() noexcept(
            noexcept(clear()) &&
            std::is_nothrow_destructible_v<address_space<T, N>>
        ) {
            clear();
        }
    };

    template <>
    struct Alloc<HEAP> {
        size_type capacity = 0;
        size_type size = 0;
        pointer storage = nullptr;

        constexpr pointer data() noexcept { return storage; }
        constexpr const_pointer data() const noexcept { return storage; }

        template <typename... Args>
        constexpr void construct(pointer p, Args&&... args) noexcept(
            noexcept(new (p) T(std::forward<Args>(args)...))
        ) {
            new (p) T(std::forward<Args>(args)...);
            ++size;
        }

        constexpr void destroy(pointer p) noexcept(
            std::is_nothrow_destructible_v<T>
        ) {
            if constexpr (!std::is_trivially_destructible_v<T>) {
                p->~T();
            }
            --size;
        }

        constexpr void grow(size_type n) {
            if (n == 0) {
                clear();
                free(storage);
                storage = nullptr;
                capacity = 0;
                return;
            }

            pointer new_storage = reinterpret_cast<pointer>(malloc(sizeof(T) * n));
            if (!new_storage) {
                throw MemoryError();
            }

            // if a move constructor or destructor fails, make a best-faith effort
            // to roll the previous contents back to the original container
            try {
                for (size_type i = 0; i < size;) {
                    try {
                        new (new_storage + i) T(std::move(storage[i]));
                        if constexpr (!std::is_trivially_destructible_v<T>) {
                            storage[i++].~T();
                        }
                    } catch (...) {
                        for (size_type j = 0; j < i; ++j) {
                            new (storage + j) T(std::move(new_storage[j]));
                            if constexpr (!std::is_trivially_destructible_v<T>) {
                                new_storage[j].~T();
                            }
                        }
                        throw;
                    }
                }
            } catch (...) {
                free(new_storage);
                throw;
            }

            free(storage);
            storage = new_storage;
            capacity = n;
        }

        constexpr void shrink(size_type n) {
            if (n == 0) {
                clear();
                free(storage);
                storage = nullptr;
                capacity = 0;
                return;
            }

            pointer new_storage = reinterpret_cast<pointer>(malloc(sizeof(T) * n));
            if (!new_storage) {
                throw MemoryError();
            }

            // if a move constructor or destructor fails, make a best-faith effort
            // to roll the previous contents back to the original container
            try {
                for (size_type i = 0; i < size;) {
                    try {
                        new (new_storage + i) T(std::move(storage[i]));
                        if constexpr (!std::is_trivially_destructible_v<T>) {
                            storage[i++].~T();
                        }
                        
                    } catch (...) {
                        for (size_type j = 0; j < i; ++j) {
                            new (storage + j) T(std::move(new_storage[j]));
                            if constexpr (!std::is_trivially_destructible_v<T>) {
                                new_storage[j].~T();
                            }
                        }
                        throw;
                    }
                }
            } catch (...) {
                free(new_storage);
                throw;
            }

            free(storage);
            storage = new_storage;
            capacity = size;
        }

        constexpr void clear() noexcept(std::is_nothrow_destructible_v<T>) {
            if constexpr (std::is_trivially_destructible_v<T>) {
                size = 0;
            } else {
                while (size) {
                    --size;
                    storage[size].~T();
                }
            }
        }

        constexpr Alloc() = default;

        constexpr Alloc(const Alloc& other) :
            size(other.size),
            capacity(other.size)
        {
            if (size) {
                storage = reinterpret_cast<pointer>(
                    malloc(sizeof(T) * size)
                );
                if (!storage) {
                    throw MemoryError();
                }
                for (size_type i = 0; i < size; ++i) {
                    try {
                        construct(storage + i, other.storage[i]);
                    } catch (...) {
                        clear();
                        free(storage);
                        throw;
                    }
                }
            }
        }

        constexpr Alloc(Alloc&& other) noexcept :
            size(other.size),
            capacity(other.capacity),
            storage(other.storage)
        {
            other.size = 0;
            other.capacity = 0;
            other.storage = nullptr;
        }

        constexpr Alloc& operator=(const Alloc& other) {
            if (this != &other) {
                clear();

                // only reallocate if new size is larger than current capacity
                if (other.size > capacity) {
                    if (storage) {
                        free(storage);
                    }
                    storage = reinterpret_cast<pointer>(
                        malloc(sizeof(T) * other.size)
                    );
                    if (!storage) {
                        throw MemoryError();
                    }
                    capacity = other.size;
                }

                while (size < other.size) {
                    try {
                        construct(storage + size, other.storage[size]);
                    } catch (...) {
                        clear();
                        throw;
                    }
                }
            }
            return *this;
        }

        constexpr Alloc& operator=(Alloc&& other) noexcept(
            noexcept(clear()) &&
            noexcept(free(storage))
        ) {
            if (this != &other) {
                clear();
                if (storage) {
                    free(storage);
                }
                size = other.size;
                capacity = other.capacity;
                storage = other.storage;
                other.size = 0;
                other.capacity = 0;
                other.storage = nullptr;
            }
            return *this;
        }

        constexpr ~Alloc() noexcept(
            noexcept(clear()) &&
            noexcept(free(storage))
        ) {
            clear();
            if (storage) {
                free(storage);
            }
            size = 0;
            capacity = 0;
            storage = nullptr;
        }
    };

    using alloc = Alloc<
        address_space<T, N>::SMALL ? PHYSICAL : (HAS_ARENA ? VIRTUAL : HEAP)
    >;
    alloc m_alloc;

public:
    /* Default constructor.  Creates an empty list and reserves virtual address space
    if `STABLE_ADDRESS == true`. */
    [[nodiscard]] constexpr List() noexcept(noexcept(alloc())) = default;

    /* Initializer list constructor. */
    [[nodiscard]] constexpr List(std::initializer_list<T> items) {
        if constexpr (meta::is_void<Less>) {
            extend(items.begin(), items.end());
        } else {
            update(items.begin(), items.end());
        }
    }

    /* Conversion constructor from an iterable range whose contents are convertible to
    the stored type `T`. */
    template <meta::yields<T> Range>
    [[nodiscard]] constexpr explicit List(Range&& range) {
        if constexpr (meta::is_void<Less>) {
            extend(std::forward<Range>(range));
        } else {
            update(std::forward<Range>(range));
        }
    }

    /* Conversion constructor from an iterator pair whose contents are convertible to
    the stored type `T`. */
    template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End>
        requires (!meta::is_const<Begin> && !meta::is_const<End>)
    [[nodiscard]] constexpr explicit List(Begin&& begin, End&& end) {
        if constexpr (meta::is_void<Less>) {
            extend(std::forward<Begin>(begin), std::forward<End>(end));
        } else {
            update(std::forward<Begin>(begin), std::forward<End>(end));
        }
    }

    /* Copy constructor.  Copies the contents of another list into a new address
    range. */
    [[nodiscard]] constexpr List(const List& other) noexcept(
        noexcept(alloc(other.alloc))
    ) = default;

    /* Move constructor.  Preserves the original addresses and simply transfers
    ownership if the list is stored on the heap or in a virtual address space.  If `N`
    is small enough to trigger the small space optimization, then the contents will be
    moved elementwise into the new list. */
    [[nodiscard]] constexpr List(List&& other) noexcept(
        noexcept(alloc(std::move(other.alloc)))
    ) = default;

    /* Copy assignment operator.  Dumps the current contents of the list and then
    copies those of another list into a new address range. */
    [[maybe_unused]] constexpr List& operator=(const List& other) noexcept(
        noexcept(m_alloc = other.m_alloc)
    ) = default;

    /* Move assignment operator.  Dumps the current contents of the list and then  */
    [[maybe_unused]] constexpr List& operator=(List&& other) noexcept(
        noexcept(m_alloc = std::move(other.m_alloc))
    ) = default;

    /* Destructor.  Calls destructors for each of the elements and then releases any
    memory held by the list.  If the list was backed by a virtual address space, then
    the address space will be released and any physical pages will be reclaimed by the
    operating system.  */
    constexpr ~List() noexcept(noexcept(m_alloc.~alloc())) = default;

    /* Swap two lists as cheaply as possible.  If an exception occurs, a best-faith
    effort is made to restore the operands to their original state. */
    constexpr void swap(List& other) & noexcept(
        noexcept(List(std::move(*this))) &&
        noexcept(*this = std::move(other)) &&
        noexcept(other = std::move(*this))
    ) {
        if (this == &other) {
            return;
        }

        List temp = std::move(*this);
        try {
            *this = std::move(other);
            try {
                other = std::move(temp);
            } catch (...) {
                other = std::move(*this);
                *this = std::move(temp);
                throw;
            }
        } catch (...) {
            *this = std::move(temp);
            throw;
        }
    }

    /* The current number of elements contained in the list. */
    [[nodiscard]] constexpr size_type size() const noexcept { return m_alloc.size; }

    /* True if the list has nonzero size.  False otherwise. */
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return size(); }

    /* True if the list has zero size.  False otherwise. */
    [[nodiscard]] constexpr bool empty() const noexcept { return size() == 0; }

    /* The total number of elements that can be stored before triggering dynamic
    growth. */
    [[nodiscard]] constexpr size_type capacity() const noexcept { return m_alloc.capacity; }

    /* The absolute maximum number of elements that can be stored within the list.
    This is equal to the `N` template parameter. */
    [[nodiscard]] static constexpr size_type max_capacity() noexcept { return N; }

    /* Estimates the total amount of memory being consumed by the list. */
    [[nodiscard]] constexpr size_type nbytes() const noexcept {
        return sizeof(List) + capacity() * sizeof(T);
    }

    /* Estimates the maximum amount of memory that the list could consume.  This is
    obtained by extrapolating from `N`, which defaults to a maximum capacity of a few
    MiB.  Note that this indicates potential memory consumption, and not the actual
    amount of memory that is currently being used. */
    [[nodiscard]] static constexpr size_type max_nbytes() noexcept {
        return sizeof(List) + N * sizeof(T);
    }

    /* Retrieve a pointer to the start of the list.  This might be null if
    `STABLE_ADDRESS == false` and the list is empty, in which case the underlying array
    will be deleted to save space. */
    [[nodiscard]] constexpr pointer data() noexcept {return m_alloc.data(); }

    /* Retrieve a pointer to the start of the list.  This might be null if
    `STABLE_ADDRESS == false` and the list is empty, in which case the underlying array
    will be deleted to save space. */
    [[nodiscard]] constexpr const_pointer data() const noexcept { return m_alloc.data(); }

    /* Retrieve a reference to the first element in the list.  Throws an `IndexError`
    if the list is empty. */
    [[nodiscard]] constexpr reference front() noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (empty()) {
                throw IndexError("empty list has no front() element");
            }
        }
        return *data();
    }

    /* Retrieve a reference to the first element in the list.  Throws an `IndexError`
    if the list is empty. */
    [[nodiscard]] constexpr const_reference front() const noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (empty()) {
                throw IndexError("empty list has no front() element");
            }
        }
        return *data();
    }

    /* Retrieve a reference to the last element in the list.  Throws an `IndexError`
    if the list is empty. */
    [[nodiscard]] constexpr reference back() noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (empty()) {
                throw IndexError("empty list has no back() element");
            }
        }
        return *(data() + size() - 1);
    }

    /* Retrieve a reference to the last element in the list.  Throws an `IndexError`
    if the list is empty. */
    [[nodiscard]] constexpr const_reference back() const noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (empty()) {
                throw IndexError("empty list has no back() element");
            }
        }
        return *(data() + size() - 1);
    }

    /* Resize the allocator to store at least `n` elements.  Returns the number of
    additional elements that were allocated. */
    [[maybe_unused]] constexpr size_type reserve(size_type n) noexcept(
        !DEBUG && noexcept(m_alloc.grow(n))
    ) {
        if (n > capacity()) {
            if constexpr (DEBUG) {
                if (n > N) {
                    throw ValueError(
                        "cannot reserve more than " + static_str<>::from_int<N> +
                        " elements"
                    );
                }
            }
            size_type result = capacity() - n;
            m_alloc.grow(n);
            return result;
        }
        return 0;
    }

    /* Resize the allocator to store at most `n` elements, reclaiming any unused
    capacity.  If `n` is less than the current size, this method will shrink only to
    that size, and no lower. */
    [[maybe_unused]] constexpr size_type shrink(size_type n = 0) noexcept(
        noexcept(m_alloc.shrink(n))
    ) {
        n = std::max(n, size());
        if (n < capacity()) {
            size_type result = capacity() - n;
            m_alloc.shrink(n);
            return result;
        }
        return 0;
    }

    [[nodiscard]] constexpr iterator begin()
        noexcept(noexcept(iterator(data(), 0, size())))
    {
        return {data(), 0, size()};
    }

    [[nodiscard]] constexpr const_iterator begin() const
        noexcept(noexcept(const_iterator(data(), 0, size())))
    {
        return {data(), 0, size()};
    }

    [[nodiscard]] constexpr const_iterator cbegin() const
        noexcept(noexcept(const_iterator(data(), 0, size())))
    {
        return {data(), 0, size()};
    }

    [[nodiscard]] constexpr iterator end()
        noexcept(noexcept(iterator(data(), size(), size())))
    {
        return {data(), size(), size()};
    }

    [[nodiscard]] constexpr const_iterator end() const
        noexcept(noexcept(const_iterator(data(), size(), size())))
    {
        return {data(), size(), size()};
    }

    [[nodiscard]] constexpr const_iterator cend() const
        noexcept(noexcept(const_iterator(data(), size(), size())))
    {
        return {data(), size(), size()};
    }

    [[nodiscard]] constexpr reverse_iterator rbegin()
        noexcept(noexcept(reverse_iterator(end())))
    {
        return std::make_reverse_iterator(end());
    }

    [[nodiscard]] constexpr const_reverse_iterator rbegin() const
        noexcept(noexcept(const_reverse_iterator(end())))
    {
        return std::make_reverse_iterator(end());
    }

    [[nodiscard]] constexpr const_reverse_iterator crbegin() const
        noexcept(noexcept(const_reverse_iterator(end())))
    {
        return std::make_reverse_iterator(end());
    }

    [[nodiscard]] constexpr reverse_iterator rend()
        noexcept(noexcept(reverse_iterator(begin())))
    {
        return std::make_reverse_iterator(begin());
    }

    [[nodiscard]] constexpr const_reverse_iterator rend() const
        noexcept(noexcept(const_reverse_iterator(begin())))
    {
        return std::make_reverse_iterator(begin());
    }

    [[nodiscard]] constexpr const_reverse_iterator crend() const
        noexcept(noexcept(const_reverse_iterator(begin())))
    {
        return std::make_reverse_iterator(begin());
    }

    /* Return an mutable iterator to a specific index of the list.  Bounds checking is
    always performed, and an `IndexError` is thrown if the index is out of range.  The
    index may be negative, in which case Python-style wraparound is applied (e.g. `-1`
    refers to the last element, `-2` to the second to last, etc.).  The resulting
    iterator can be assigned to in order to simulate indexed assignment into the list,
    and can be implicitly converted to arbitrary types as if it were a typical
    reference. */
    [[nodiscard]] constexpr iterator operator[](index_type i) {
        return {data(), normalize_index(i), size()};
    }

    /* Return an immutable iterator to a specific index of the list.  Bounds checking
    is always performed, and an `IndexError` is thrown if the index is out of range.
    The index may be negative, in which case Python-style wraparound is applied (e.g.
    `-1` refers to the last element, `-2` to the second to last, etc.).  The resulting
    iterator can be implicitly converted to arbitrary types as if it were a typical
    reference. */
    [[nodiscard]] constexpr const_iterator operator[](index_type i) const {
        return {data(), normalize_index(i), size()};
    }

    /// TODO: operator[{start, stop, step}] -> slice
    ///     slice.count(value)
    ///     slice.index(value)
    ///     conversions
    ///     assignment

    /* Insert an item at the end of the list.  All arguments are forwarded to the
    constructor for `T`.  Returns an iterator to the appended element, and propagates
    any errors emanating from the constructor, without modifying the list. */
    template <typename... Args>
        requires (std::constructible_from<T, Args...> && !SORTED)
    [[maybe_unused]] constexpr iterator append(Args&&... args) noexcept(
        noexcept(m_alloc.grow(std::min(N, capacity() * 2))) &&
        noexcept(m_alloc.construct(data(), std::forward<Args>(args)...))
    ) {
        if (size() == capacity()) {
            if (size() == N) {
                throw ValueError(
                    "cannot append to list with more than " +
                    static_str<>::from_int<N> + " elements"
                );
            }
            m_alloc.grow(std::min(N, capacity() * 2));
        }
        m_alloc.construct(m_alloc.data() + size(), std::forward<Args>(args)...);
        return {data(), size() - 1, size()};
    }

    /* Insert an item at an arbitrary location within the list, immediately before the
    given iterator.  All subsequent elements will be shifted one index down the list.
    If the iterator is out of bounds, it will be truncated to the nearest valid
    position.  All other arguments are forwarded to the constructor for `T`, and any
    errors will be propagated, without modifying the state of the list.  Returns an
    iterator to the inserted element. */
    template <typename... Args>
        requires (std::constructible_from<T, Args...> && !SORTED)
    [[maybe_unused]] constexpr iterator insert(iterator it, Args&&... args) noexcept(
        !DEBUG &&
        noexcept(m_alloc.grow(std::min(N, capacity() * 2))) &&
        noexcept(m_alloc.construct(data(), std::forward<Args>(args)...)) &&
        noexcept(m_alloc.destroy(data()))
    ) {
        // truncate iterator to bounds of list
        if (it < begin()) {
            it = begin();
        } else if (it > end()) {
            it = end();
        }

        // grow if necessary
        if (size() == capacity()) {
            if constexpr (DEBUG) {
                if (size() == N) {
                    throw ValueError(
                        "list exceeds maximum length (" + static_str<>::from_int<N> + ")"
                    );
                }
            }
            m_alloc.grow(std::min(N, capacity() * 2));
        }

        // move all subsequent elements down one index
        for (size_type i = it.m_index; i < size();) {
            try {
                m_alloc.construct(
                    m_alloc.data() + i + 1,
                    std::move(m_alloc.data()[i])
                );
                m_alloc.destroy(m_alloc.data() + (i++));
            } catch (...) {
                for (size_type j = it.m_index; j < i; ++j) {
                    m_alloc.construct(
                        m_alloc.data() + j,
                        std::move(m_alloc.data()[j + 1])
                    );
                    m_alloc.destroy(m_alloc.data() + j + 1);
                }
                throw;
            }
        }

        // insert new element at iterator position
        try {
            m_alloc.construct(data() + it.m_index, std::forward<Args>(args)...);
        } catch (...) {
            for (size_type j = it.m_index; j < size(); ++j) {
                m_alloc.construct(
                    m_alloc.data() + j,
                    std::move(m_alloc.data()[j + 1])
                );
                m_alloc.destroy(m_alloc.data() + j + 1);
            }
            throw;
        }

        // return an iterator to the inserted element
        return {data(), it.m_index, size()};
    }

    /* Insert an item into a sorted list, maintaining proper order.  A binary search
    will be performed to find the proper insertion point, and all subsequent elements
    will be shifted one index down the list.  If an item compares equal to the inserted
    value, then the final ordering between them is not defined.  All other arguments
    are forwarded to the constructor for `T`. */
    template <typename... Args>
        requires (std::constructible_from<T, Args...> && SORTED)
    [[maybe_unused]] constexpr iterator insert(Args&&... args) noexcept(
        true  // TODO: fix this
    ) {
        /// TODO: construct a temporary item, then binary search for the
        /// insertion point, then move all subsequent elements down one index and
        /// insert the new item at the insertion point
    }

    /* Insert items from an input range at the end of the list, returning the number of
    items that were inserted.  In the event of an error, the list will be rolled back
    to its original state before propagating the error. */
    template <typename Dummy = Less> requires (!SORTED)
    [[maybe_unused]] constexpr size_type extend(std::initializer_list<T> items) {
        return extend(items.begin(), items.end());
    }

    /* Insert items from an input range at the end of the list, returning the number of
    items that were inserted.  In the event of an error, the list will be rolled back
    to its original state before propagating the error. */
    template <meta::yields<value_type> Range> requires (!SORTED)
    [[maybe_unused]] constexpr size_type extend(Range&& range) {
        using RangeRef = std::add_lvalue_reference_t<Range>;
        if constexpr (meta::has_size<RangeRef>) {
            reserve(size() + std::ranges::size(range));
        }

        size_type old_size = size();
        auto begin = std::ranges::begin(range);
        auto end = std::ranges::end(range);
        while (begin != end) {
            if constexpr (!meta::has_size<RangeRef>) {
                if (size() == capacity()) {
                    if constexpr (DEBUG) {
                        if (size() == N) {
                            throw ValueError(
                                "list exceeds maximum length (" +
                                static_str<>::from_int<N> + ")"
                            );
                        }
                    }
                    m_alloc.grow(std::min(N, capacity() * 2));
                }
            }
            try {
                m_alloc.construct(m_alloc.data() + size(), *begin);
            } catch (...) {
                size_type new_size = size();
                for (size_t j = old_size; j < new_size; ++j) {
                    m_alloc.destroy(m_alloc.data() + j);
                }
                throw;
            }
            ++begin;
        }
        return size() - old_size;
    }

    /* Insert items from an input range at the end of the list, returning the number of
    items that were inserted.  In the event of an error, the list will be rolled back
    to its original state before propagating the error. */
    template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End>
        requires (meta::dereferences_to<Begin, T> && !SORTED)
    [[maybe_unused]] constexpr size_type extend(Begin&& begin, End&& end) {
        using BeginRef = std::add_lvalue_reference_t<Begin>;
        using EndRef = std::add_lvalue_reference_t<End>;
        if constexpr (meta::sub_returns<EndRef, BeginRef, size_type>) {
            reserve(end - begin);
        }

        size_type old_size = size();
        while (begin != end) {
            if constexpr (!meta::sub_returns<EndRef, BeginRef, size_type>) {
                if (size() == capacity()) {
                    if constexpr (DEBUG) {
                        if (size() == N) {
                            throw ValueError(
                                "list exceeds maximum length (" +
                                static_str<>::from_int<N> + ")"
                            );
                        }
                    }
                    m_alloc.grow(std::min(N, capacity() * 2));
                }
            }
            try {
                m_alloc.construct(m_alloc.data() + size(), *begin);
            } catch (...) {
                size_type new_size = size();
                for (size_t j = old_size; j < new_size; ++j) {
                    m_alloc.destroy(m_alloc.data() + j);
                }
                throw;
            }
            ++begin;
        }
        return size() - old_size;
    }

    /* Insert items from an input range into a sorted list, maintaining proper order.
    Returns the number of items that were inserted.  In the event of an error, the list
    will be rolled back to its original state before propagating the error. */
    template <typename Dummy = Less> requires (SORTED)
    [[maybe_unused]] constexpr size_type update(std::initializer_list<T> items) {
        return update(items.begin(), items.end());
    }

    /* Insert items from an input range into a sorted list, maintaining proper order.
    Returns the number of items that were inserted.  In the event of an error, the list
    will be rolled back to its original state before propagating the error. */
    template <meta::yields<value_type> Range> requires (SORTED)
    [[maybe_unused]] constexpr size_type update(Range&& range) {
        /// TODO: basically identical to extend() for the sorted case

        if constexpr (!meta::is_void<Less>) {
            size_type new_size = size();
            if (new_size > old_size) {
                try {
                    sort(Less{});
                } catch (...) {
                    /// TODO: at this point, the list should be back in its original
                    /// state, so I can just remove the new elements and throw
                    for (size_type j = old_size; j < new_size; ++j) {
                        m_alloc.destroy(m_alloc.data() + j);
                    }
                    throw;
                }
            }
        }

        return size() - old_size;
    }

    /* Insert items from an input range into a sorted list, maintaining proper order.
    Returns the number of items that were inserted.  In the event of an error, the list
    will be rolled back to its original state before propagating the error. */
    template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End>
        requires (meta::dereferences_to<Begin, T> && SORTED)
    [[maybe_unused]] constexpr size_type update(Begin&& begin, End&& end) {
        /// TODO: basically identical to extend() for the sorted case

        if constexpr (!meta::is_void<Less>) {
            size_type new_size = size();
            if (new_size > old_size) {
                try {
                    sort(Less{});
                } catch (...) {
                    /// TODO: at this point, the list should be back in its original
                    /// state, so I can just remove the new elements and throw
                    for (size_type j = old_size; j < new_size; ++j) {
                        m_alloc.destroy(m_alloc.data() + j);
                    }
                    throw;
                }
            }
        }

        return size() - old_size;
    }

    /* Efficiently remove the last item in the list.  If the list is empty and the
    program is compiled in debug mode, this will throw an `IndexError`. */
    void remove() noexcept(!DEBUG && noexcept(m_alloc.destroy(data() + size()))) {
        if constexpr (DEBUG) {
            if (empty()) {
                throw IndexError("cannot remove from empty list");
            }
        }
        m_alloc.destroy(data() + size());
    }

    /* Remove the item pointed to by the given iterator.  All subsequent elements will
    be shifted one index up the list.  If the iterator is out of bounds and the program
    is compiled in debug mode, this will throw an `IndexError`. */
    void remove(iterator it) {
        if constexpr (DEBUG) {
            if (it < begin() || it >= end()) {
                throw IndexError("iterator out of bounds");
            }
        }
        m_alloc.destroy(data() + it.m_index);
        for (size_type i = it.m_index; i < size(); ++i) {
            m_alloc.construct(
                m_alloc.data() + i,
                std::move(m_alloc.data()[i + 1])
            );
            m_alloc.destroy(m_alloc.data() + i + 1);
        }
    }

    /// TODO: remove(), remove(it), remove(slice)

    /// TODO: pop(), pop(it), pop(slice)

    /* Remove all elements from the list, resetting the size to zero, but leaving the
    capacity unchanged. */
    void clear() noexcept(noexcept(m_alloc.clear())) {
        m_alloc.clear();
    }

    /// TODO: find(value).  Either a linear search or binary search depending on
    /// template configuration

    /// TODO: contains().  Equuivalent to find() != end()

    /// TODO: count(value).  call find(), and then iterate forwards and maybe also
    /// backwards to count the number of occurrences.

    /// TODO: index(value).  call find() and note that a binary search might not
    /// strictly give the first occurrence, so I would need to backwards iterate until
    /// I find the next smallest item and then report the first index.

    /// TODO: sort(compare) (timsort)

    /// TODO: lexicographic comparisons against any iterable whose value type can be
    /// passed to less<> or the transparent `<` operator.

    /// TODO: operator+

    /// TODO: operator+=

    /// TODO: operator*

    /// TODO: operator*=

    /// TODO: also dereference and ->* operators, for compatibility with functions

    /// TODO: structured bindings if N is small enough?

};


}  // namespace bertrand


namespace std {


}


#endif
