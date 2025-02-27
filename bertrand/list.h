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
    meta::allocator_for<T> Alloc = std::allocator<T>
>
    requires (!meta::reference<T> && !meta::is_void<T>)
struct List;


namespace impl {

    /* Common immutable, bounds-checked iterator class for `bertrand::list`. */
    template <typename T> requires (!meta::reference<T> && !meta::is_void<T>)
    struct const_list_iterator {
        using iterator_category = std::contiguous_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = const T;
        using pointer = const value_type*;
        using reference = const value_type&;

    private:
        template <size_t N, meta::allocator_for<T> Alloc>
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

    /* Common mutable, bounds-checked iterator class for `bertrand::list`. */
    template <typename T> requires (!meta::reference<T> && !meta::is_void<T>)
    struct list_iterator {
        using iterator_category = std::contiguous_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using pointer = value_type*;
        using reference = value_type&;

    private:
        template <size_t N, meta::allocator_for<T> Alloc>
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

}


/* A simple dynamic array that can use either an STL-compliant allocator class, a
virtual address space (the default), or a fixed-size physical array as the underlying
storage backend.

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
template <typename T, size_t N, meta::allocator_for<T> Alloc>
    requires (!meta::reference<T> && !meta::is_void<T>)
struct List {
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

private:

    template <typename V>
    static constexpr V& lvalue(V&& x) noexcept { return x; }

    static constexpr void invoke_destructors(T* data, size_t size)
        noexcept(std::is_nothrow_destructible_v<T>)
    {
        if constexpr (!std::is_trivially_destructible_v<T>) {
            for (size_t i = 0; i < size; ++i) {
                data[i].~T();
            }
        }
    }

    constexpr size_t normalize_index(index_type i) {
        if (i < 0) {
            i += size();
            if (i < 0) {
                throw IndexError(std::to_string(i));
            }
        } else if (i >= size()) {
            throw IndexError(std::to_string(i));
        }
        return static_cast<size_t>(i);
    }

    /// TODO: maybe copy/move assignment operators can reuse the same memory, without
    /// needing to reallocate?
    /// TODO: copy/move constructors can be implemented on the backend storage class
    /// and then called polymorphically.  That should also handle the physical case
    /// where there are no copy/move constructors for the space itself.

    template <bool cond>
    struct backend {
        address_space<T, N> space;
        size_type size = 0;
        size_type capacity = 0;

        constexpr backend(const backend& other) noexcept(
            noexcept(space.allocate(0, other.size)) &&
            noexcept(space.construct(space.data(), *other.space.data()))
        ) {
            T* data = space.allocate(0, other.size);
            for (size_type i = 0; i < other.size; ++i) {
                try {
                    space.construct(data + i, other.space.data()[i]);
                } catch (...) {
                    invoke_destructors(data, i);
                    throw;
                }
            }
        }

        constexpr backend(backend&& other) noexcept(address_space<T, N>::SMALL ?
            (
                noexcept(address_space<T, N>{}) &&
                noexcept(space.allocate(0, other.size)) &&
                noexcept(space.construct(space.data(), std::move(*other.space.data())))
            ) :
            noexcept(address_space<T, N>(std::move(other.space)))
        ) :
            space([](backend&& other) {
                // if the address space is small-size optimized, then we have to move
                // the contents elementwise into a new space
                if constexpr (address_space<T, N>::SMALL) {
                    address_space<T, N> space;
                    T* data = space.allocate(0, other.size);
                    for (size_type i = 0; i < other.size; ++i) {
                        try {
                            space.construct(
                                data + i,
                                std::move(other.space.data()[i])
                            );

                        // if a move constructor fails, make a best-faith effort to
                        // move the previous contents back to the original container
                        } catch (...) {
                            for (size_type j = 0; j < i; ++j) {
                                try {
                                    other.space.construct(
                                        other.data() + j,
                                        std::move(data[j])
                                    );
                                } catch (...) {
                                    invoke_destructors(data, i);
                                    throw;
                                }
                            }
                            invoke_destructors(data, i);
                            throw;
                        }
                    }
                    return space;

                // otherwise, we can just transfer ownership
                } else {
                    return std::move(other.space);
                }
            }(std::move(other)))
        {}

        constexpr backend& operator=(const backend& other) {
            if (this != &other) {
                /// TODO:
            }
            return *this;
        }

        constexpr backend& operator=(backend&& other) {
            if (this != &other) {
                /// TODO:
            }
            return *this;
        }

        constexpr ~backend() noexcept(std::is_nothrow_destructible_v<T>) {
            if (space) {
                invoke_destructors(space.data(), size);
            }
        }

        constexpr T* data() const noexcept(noexcept(space.data())) { return space.data(); }

        constexpr void reserve(size_type n)
            noexcept(noexcept(space.allocate(capacity, n - capacity)))
        {
            if (n > capacity) {
                space.allocate(capacity, n - capacity);
                capacity = n;
            }
        }

        constexpr void shrink()
            noexcept(noexcept(space.deallocate(size, capacity - size)))
        {
            if (size < capacity) {
                space.deallocate(size, capacity - size);
                capacity = size;
            }
        }

        template <typename... Args>
        constexpr void construct(pointer p, Args&&... args) noexcept(
            noexcept(space.construct(space.data(), std::forward<Args>(args)...))
        ) {
            space.construct(p, std::forward<Args>(args)...);
            ++size;
        }

        constexpr void destroy(pointer p) noexcept(
            noexcept(space.destroy(space.data()))
        ) {
            space.destroy(p);
            --size;
        }
    };

    template <>
    struct backend<false> {
        Alloc heap;
        T* storage = nullptr;
        size_t size = 0;
        size_t capacity = 0;

        [[nodiscard]] constexpr T* data() const noexcept { return storage; }

        // constexpr backend(const backend& other)

        constexpr ~backend() noexcept(noexcept(std::free(storage))) {
            if (storage) {
                invoke_destructors(storage, size);
                std::free(storage);
                storage = nullptr;
            }
        }
    };

    using alloc = backend<HAS_ARENA>;
    alloc m_alloc;

public:
    /* The default capacity to reserve if no template override is given.  This defaults
    to a maximum of ~8 MiB of total storage, evenly divided into contiguous segments of
    type `T`. */
    static constexpr bool DEFAULT_CAPACITY = impl::DEFAULT_ADDRESS_CAPACITY<T>;

    /* True if the container is backed by a stable address space, meaning that dynamic
    growth will not invalidate pointers to existing elements.  This is equivalent to
    the `bertrand::HAS_ARENA` flag */
    static constexpr bool STABLE_ADDRESS = HAS_ARENA;

    /* Default constructor.  Creates an empty list and reserves virtual address space
    if `STABLE_ADDRESS == true`. */
    [[nodiscard]] constexpr List() noexcept(noexcept(alloc())) = default;

    /* Initializer list constructor. */
    [[nodiscard]] constexpr List(std::initializer_list<T> items) noexcept(
        noexcept(list(items.begin(), items.end()))
    ) : List(items.begin(), items.end())
    {}

    /* Conversion constructor from an iterable range whose contents are convertible to
    the stored type `T`. */
    template <meta::yields<T> Range>
    [[nodiscard]] constexpr explicit List(Range&& range) noexcept(
        noexcept(alloc()) &&
        (
            !meta::has_size<std::add_lvalue_reference_t<Range>> ||
            meta::has_nothrow_size<std::add_lvalue_reference_t<Range>>
        ) &&
        noexcept(lvalue(std::ranges::begin(range)) != lvalue(std::ranges::end(range))) &&
        noexcept(m_alloc.construct(
            m_alloc.data() + size(),
            *lvalue(std::ranges::begin(range))
        )) &&
        noexcept(++lvalue(std::ranges::begin(range)))
    ) {
        using RangeRef = std::add_lvalue_reference_t<Range>;
        if constexpr (meta::has_size<RangeRef>) {
            reserve(std::ranges::size(range));
        }
        auto begin = std::ranges::begin(range);
        auto end = std::ranges::end(range);
        while (begin != end) {
            if constexpr (!meta::has_size<RangeRef>) {
                reserve(size() + 1);
            }
            try {
                m_alloc.construct(m_alloc.data() + size(), *begin);
            } catch (...) {
                invoke_destructors();
                throw;
            }
            ++begin;
        }
    }

    /* Conversion constructor from an iterator pair whose contents are convertible to
    the stored type `T`. */
    template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End>
        requires (!meta::is_const<Begin> && !meta::is_const<End>)
    [[nodiscard]] constexpr explicit List(Begin&& begin, End&& end) noexcept(
        noexcept(alloc()) &&
        (
            !meta::sub_returns<
                std::add_lvalue_reference_t<End>,
                std::add_lvalue_reference_t<Begin>,
                size_t
            > ||
            meta::has_nothrow_sub<
                std::add_lvalue_reference_t<End>,
                std::add_lvalue_reference_t<Begin>
            >
        ) &&
        noexcept(lvalue(begin) != lvalue(end)) &&
        noexcept(m_alloc.construct(m_alloc.data() + size(), *lvalue(begin))) &&
        noexcept(++lvalue(begin))
    ) {
        using BeginRef = std::add_lvalue_reference_t<Begin>;
        using EndRef = std::add_lvalue_reference_t<End>;
        if constexpr (meta::sub_returns<EndRef, BeginRef, size_t>) {
            reserve(end - begin);
        }
        while (begin != end) {
            if constexpr (!meta::sub_returns<EndRef, BeginRef, size_t>) {
                reserve(size() + 1);
            }
            try {
                m_alloc.construct(m_alloc.data() + size(), *begin);
            } catch (...) {
                invoke_destructors();
                throw;
            }
            ++begin;
        }
    }

    /* Copy constructor.  Copies the contents of another list into a new address
    range. */
    [[nodiscard]] constexpr List(const List& other) = default;

    /* Move constructor.  Preserves the original addresses and simply transfers
    ownership if the list is stored on the heap or in a virtual address space.  If `N`
    is small enough to trigger the small space optimization, then the contents will be
    moved elementwise into the new list. */
    [[nodiscard]] constexpr List(List&& other) = default;

    /* Copy assignment operator.  Dumps the current contents of the list and then
    copies those of another list into a new address range. */
    [[maybe_unused]] constexpr List& operator=(const List& other) = default;

    /* Move assignment operator.  Dumps the current contents of the list and then  */
    [[maybe_unused]] constexpr List& operator=(List&& other) = default;

    /* Destructor.  Calls destructors for each of the elements and then releases any
    memory held by the list.  If the list was backed by a virtual address space, then
    the address space will be released and any physical pages will be reclaimed by the
    operating system.  */
    constexpr ~List() noexcept(noexcept(m_alloc.~alloc())) = default;

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

    /* Resize the allocator to store at least the given number of elements. */
    constexpr void reserve(size_type n) noexcept(noexcept(m_alloc.reserve(n))) {
        m_alloc.reserve(n);
    }

    /* Resize the allocator to store only `size()` elements, reclaiming any unused
    capacity. */
    constexpr void shrink() noexcept(noexcept(m_alloc.shrink())) {
        m_alloc.shrink();
    }

    [[nodiscard]] iterator begin()
        noexcept(noexcept(iterator(data(), 0, size())))
    {
        return {data(), 0, size()};
    }

    [[nodiscard]] const_iterator begin() const
        noexcept(noexcept(const_iterator(data(), 0, size())))
    {
        return {data(), 0, size()};
    }

    [[nodiscard]] const_iterator cbegin() const
        noexcept(noexcept(const_iterator(data(), 0, size())))
    {
        return {data(), 0, size()};
    }

    [[nodiscard]] iterator end()
        noexcept(noexcept(iterator(data(), size(), size())))
    {
        return {data(), size(), size()};
    }

    [[nodiscard]] const_iterator end() const
        noexcept(noexcept(const_iterator(data(), size(), size())))
    {
        return {data(), size(), size()};
    }

    [[nodiscard]] const_iterator cend() const
        noexcept(noexcept(const_iterator(data(), size(), size())))
    {
        return {data(), size(), size()};
    }

    [[nodiscard]] reverse_iterator rbegin()
        noexcept(noexcept(reverse_iterator(end())))
    {
        return std::make_reverse_iterator(end());
    }

    [[nodiscard]] const_reverse_iterator rbegin() const
        noexcept(noexcept(const_reverse_iterator(end())))
    {
        return std::make_reverse_iterator(end());
    }

    [[nodiscard]] const_reverse_iterator crbegin() const
        noexcept(noexcept(const_reverse_iterator(end())))
    {
        return std::make_reverse_iterator(end());
    }

    [[nodiscard]] reverse_iterator rend()
        noexcept(noexcept(reverse_iterator(begin())))
    {
        return std::make_reverse_iterator(begin());
    }

    [[nodiscard]] const_reverse_iterator rend() const
        noexcept(noexcept(const_reverse_iterator(begin())))
    {
        return std::make_reverse_iterator(begin());
    }

    [[nodiscard]] const_reverse_iterator crend() const
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
    [[nodiscard]] iterator operator[](index_type i) {
        return {data(), normalize_index(i), size()};
    }

    /* Return an immutable iterator to a specific index of the list.  Bounds checking
    is always performed, and an `IndexError` is thrown if the index is out of range.
    The index may be negative, in which case Python-style wraparound is applied (e.g.
    `-1` refers to the last element, `-2` to the second to last, etc.).  The resulting
    iterator can be implicitly converted to arbitrary types as if it were a typical
    reference. */
    [[nodiscard]] const_iterator operator[](index_type i) const {
        return {data(), normalize_index(i), size()};
    }


    /// TODO: grow geometrically, not linearly.

    /* Insert an item at the end of the list. */
    template <typename... Args> requires (std::constructible_from<T, Args...>)
    void append(Args&&... args) noexcept(
        noexcept(reserve(size() + 1)) && noexcept(T(std::forward<Args>(args)...))
    ) {
        reserve(size() + 1);
        m_alloc.construct(m_alloc.data() + size(), std::forward<Args>(args)...);
    }


    /// TODO: extend(range), extend(it, end) with error recovery

    /// TODO: clear()

    /// TODO: remove(), remove(value), remove(it), remove(slice)

    /// TODO: pop(), pop(value), pop(it), pop(slice)

    /// TODO: count(value)

    /// TODO: index(value)

    /// TODO: sort(compare) (quicksort)

    /// TODO: operator[{start, stop, step}] -> slice
    ///     slice.count(value)
    ///     slice.index(value)
    ///     conversions
    ///     assignment

    /// TODO: operator+

    /// TODO: operator+=

    /// TODO: operator*

    /// TODO: operator*=

    /// TODO: also dereference and ->* operators, for compatibility with functions

    /// TODO: structured bindings if N is low enough?

};


}  // namespace bertrand


#endif
