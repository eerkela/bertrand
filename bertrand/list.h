#ifndef BERTRAND_LIST_H
#define BERTRAND_LIST_H


#include "bertrand/common.h"
#include "bertrand/allocate.h"
#include "bertrand/except.h"


namespace bertrand {


/// TODO: Some bertrand internals will need to be able to function in cases where
/// virtual addressing is not enabled, meaning the heap will be used instead.  This
/// only really affects the function infrastructure, since the overload trie will
/// prefer to use address spaces if they are available, and fall back to heap
/// allocators otherwise.  The actual caches will always use fixed physical space, so
/// that's not a problem.


namespace impl {

    template <typename T, typename Alloc>
    concept valid_list_config =
        !meta::reference<T> &&
        !meta::is_void<T> &&
        meta::unqualified<Alloc> &&
        meta::allocator_or_space_for<Alloc, T>;

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
template <typename T, typename Alloc = std::allocator<T>>
    requires (impl::valid_list_config<T, Alloc>)
struct list {
private:

    Alloc m_alloc;
    size_t m_size = 0;


public:
    /// TODO: this is actually the standard allocator case, so it works like a typical
    /// dynamic array

    list() noexcept(noexcept(Alloc())) = default;

    list(const Alloc& allocator) noexcept(noexcept(Alloc(allocator))) :
        m_alloc(allocator)
    {}

    list(Alloc&& allocator) noexcept(noexcept(Alloc(std::move(allocator)))) :
        m_alloc(std::move(allocator))
    {}

    list(std::initializer_list<T> items) noexcept(noexcept(list(items.begin(), items.end()))) :
        list(items.begin(), items.end())
    {}

    list(std::initializer_list<T> items, Alloc&& allocator) noexcept(
        noexcept(list(items.begin(), items.end(), std::move(allocator)))
    ) : list(items.begin(), items.end(), std::move(allocator))
    {}

    list(std::initializer_list<T> items, const Alloc& allocator) noexcept(
        noexcept(list(items.begin(), items.end(), allocator))
    ) : list(items.begin(), items.end(), allocator)
    {}

    template <meta::yields<T> Range>
    explicit list(Range&& range) noexcept(noexcept(Alloc())) {
        /// TODO: add all items
    }

    template <meta::yields<T> Range>
    explicit list(Range&& range, Alloc&& allocator) noexcept(
        noexcept(Alloc(std::move(allocator)))
    ) :
        m_alloc(std::move(allocator))
    {
        /// TODO: add all items
    }

    template <meta::yields<T> Range>
    explicit list(Range&& range, const Alloc& allocator) noexcept(
        noexcept(Alloc(allocator))
    ) :
        m_alloc(allocator)
    {
        /// TODO: add all items
    }

    template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End>
    explicit list(Begin&& begin, End&& end) noexcept(noexcept(Alloc())) {
        /// TODO: add all items
    }

    template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End>
    explicit list(Begin&& begin, End&& end, Alloc&& allocator) noexcept(
        noexcept(Alloc(std::move(allocator)))
    ) : m_alloc(std::move(allocator)) {
        /// TODO: add all items
    }

    template <std::input_or_output_iterator Begin, std::sentinel_for<Begin> End>
    explicit list(Begin&& begin, End&& end, const Alloc& allocator) noexcept(
        noexcept(Alloc(allocator))
    ) : m_alloc(allocator) {
        /// TODO: add all items
    }

    /// TODO: copy/move constructors and assignment operators get a little funky,
    /// especially if I want to support propagating allocators.


    template <typename... Args> requires (std::constructible_from<T, Args...>)
    void append(Args&&... args) {
        if constexpr (meta::address_space<Alloc>) {
            if constexpr (Alloc::STATIC) {
                // if ()

                T* ptr = m_alloc.allocate(m_size, 1);
                if (ptr == nullptr) {
                    throw MemoryError(std::format(
                        "Failed to allocate address at offset {} in virtual "
                        "address space starting at address {:#x} and ending at "
                        "address {:#x} ({:.2e} MiB)",
                        m_size,
                        reinterpret_cast<uintptr_t>(m_alloc.begin()),
                        reinterpret_cast<uintptr_t>(m_alloc.end()),
                        double(m_alloc.nbytes()) / double(impl::bytes::MiB)
                    ));
                }
            } else {
                if (m_size == m_alloc.size()) {

                }

            }
        } else {
            /// TODO: normal allocator case
        }
    }


};


/* Specialization for lists (dynamic arrays) that are backed by a virtual address
space. */
template <typename T, typename Alloc>
    requires (impl::valid_list_config<T, Alloc> && meta::address_space<Alloc>)
struct list<T, Alloc> {
private:

    Alloc m_alloc;
    size_t m_size = 0;

    static size_t initial_size() noexcept {
        constexpr size_t min_elements = 8;
        return (sizeof(T) * min_elements + PAGE_SIZE - 1) / PAGE_SIZE;
    }

public:

    /// TODO: constructors first


    /// TODO: initial size equates to 1 full page or enough pages to store 8 instances
    /// of T.  This is probably just the ceiling of the size of 8 instances of T with
    /// respect to page size.


    template <typename... Args> requires (std::constructible_from<T, Args...>)
    void append(Args&&... args) {
        if constexpr (Alloc::STATIC) {
            /// TODO: initial

            // if ()

            T* ptr = m_alloc.allocate(m_size, 1);
            if (ptr == nullptr) {
                throw MemoryError(std::format(
                    "Failed to allocate address at offset {} in virtual "
                    "address space starting at address {:#x} and ending at "
                    "address {:#x} ({:.2e} MiB)",
                    m_size,
                    reinterpret_cast<uintptr_t>(m_alloc.begin()),
                    reinterpret_cast<uintptr_t>(m_alloc.end()),
                    double(m_alloc.nbytes()) / double(impl::bytes::MiB)
                ));
            }
        } else {
            if (m_size == m_alloc.size()) {

            }

        }
    }

};


/* Specialization for lists (dynamic arrays) that are backed by a fixed-size, physical
array that is allocated all at once, and cannot be resized. */
template <typename T, typename Alloc>
    requires (impl::valid_list_config<T, Alloc> && meta::physical_space<Alloc>)
struct list<T, Alloc> {
    using allocator_type = Alloc;
    using size_type = allocator_type::size_type;
    using difference_type = allocator_type::difference_type;
    using value_type = allocator_type::value_type;
    using reference = allocator_type::reference;
    using const_reference = allocator_type::const_reference;
    using pointer = allocator_type::pointer;
    using const_pointer = allocator_type::const_pointer;

    /// TODO: iterators

private:

    allocator_type m_alloc;
    size_type m_size = 0;

public:




};


}  // namespace bertrand


#endif
