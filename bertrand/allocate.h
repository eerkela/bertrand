#ifndef BERTRAND_ALLOCATE_H
#define BERTRAND_ALLOCATE_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/union.h"


namespace bertrand {


namespace impl {
    struct arena_tag {};

    /* A raw number of bytes.  Interconvertible with `size_t`, and usually produced
    through an integer literal of the form `94_B`, `42_KiB`, `27_MiB`, etc. */
    struct nbytes {
        size_t value;
        constexpr nbytes(size_t value = 0) noexcept : value(value) {}
        constexpr operator size_t() const noexcept { return value; }
    };

}


/* User-defined literal returning an integer-like value corresponding to a raw
number of bytes, for use in defining fixed-size containers.  The result is
implicitly convertible to `unsigned long long`. */
constexpr impl::nbytes operator""_B(unsigned long long size) noexcept {
    return {size};
}


/* User-defined literal returning an integer-like value corresponding to a raw
number of kilobytes, for use in defining fixed-size containers.  The result is
implicitly convertible to `unsigned long long`. */
constexpr impl::nbytes operator""_KiB(unsigned long long size) noexcept {
    return {size * 1024_B};
}


/* User-defined literal returning an integer-like value corresponding to a raw
number of megabytes, for use in defining fixed-size containers.  The result is
implicitly convertible to `unsigned long long`. */
constexpr impl::nbytes operator""_MiB(unsigned long long size) noexcept {
    return {size * 1024_KiB};
}


/* User-defined literal returning an integer-like value corresponding to a raw
number of gigabytes, for use in defining fixed-size containers.  The result is
implicitly convertible to `unsigned long long`. */
constexpr impl::nbytes operator""_GiB(unsigned long long size) noexcept {
    return {size * 1024_MiB};
}


/* User-defined literal returning an integer-like value corresponding to a raw
number of terabytes, for use in defining fixed-size containers.  The result is
implicitly convertible to `unsigned long long`. */
constexpr impl::nbytes operator""_TiB(unsigned long long size) noexcept {
    return {size * 1024_GiB};
}


#ifdef BERTRAND_PAGE_SIZE
    constexpr size_t PAGE_SIZE = BERTRAND_PAGE_SIZE;
#else
    constexpr size_t PAGE_SIZE = 4_KiB;
#endif
static_assert(
    PAGE_SIZE > 0,
    "page size must be a positive number of bytes"
);


#ifdef BERTRAND_ARENA_SIZE
    constexpr size_t ARENA_SIZE = BERTRAND_ARENA_SIZE;
#else
    constexpr size_t ARENA_SIZE = 128_GiB;
#endif
static_assert(
    ARENA_SIZE % PAGE_SIZE == 0,
    "arena size must be a multiple of the page size"
);
static_assert(
    ARENA_SIZE == 0 || ARENA_SIZE >= (PAGE_SIZE * 2),
    "arena must contain at least two pages of memory to store a freelist and "
    "public partition"
);


namespace impl {

    template <typename U>
    static constexpr size_t capacity_size = sizeof(U);  // never returns 0
    template <meta::is_void U>
    static constexpr size_t capacity_size<U> = 1;

    template <typename U>
    static constexpr size_t capacity_alignment = alignof(U);  // never returns 0
    template <meta::is_void U>
    static constexpr size_t capacity_alignment<U> = 1;

    /* A self-aligning helper for defining the capacity of a fixed-size container using
    either explicit units or raw integers, which are interpreted in units of `T`.  Can
    also be in an empty state, which indicates a dynamic capacity.

    Usually, this class is used in a template signature or constructor like so:

        ```
        template <typename T, impl::capacity<T> N = None>
        struct Foo {
            Foo(impl::capacity<T> n) requires (!N) { ... }
        };

        using Bar = Foo<int, 1024>;  // static capacity
        Foo<double> baz{2048};  // dynamic capacity
        ```
    */
    template <typename T>
    struct capacity {
        static constexpr size_t aligned_size =
            capacity_size<T> + (capacity_alignment<T> - 1) / capacity_alignment<T>;

        size_t value;

        [[nodiscard]] constexpr capacity(size_t size = 0) noexcept : value(size) {}
        [[nodiscard]] constexpr capacity(impl::nbytes size) noexcept :
            value((size.value / aligned_size))
        {}

        /* The total number of bytes needed to represent the given number of instances
        of type `T`. */
        [[nodiscard]] constexpr size_t bytes() const noexcept {
            return value * aligned_size;
        }

        /* Align the capacity to the nearest physical page, increasing it to a multiple
        of the `PAGE_SIZE`, and then adjusting it downwards to account for the
        alignment of `T`. */
        [[nodiscard]] constexpr capacity page_align() const noexcept {
            size_t pages = ((value * aligned_size + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;
            return pages / aligned_size;
        }

        [[nodiscard]] constexpr size_t operator*() const noexcept {
            return value;
        }

        [[nodiscard]] constexpr explicit operator bool() const noexcept {
            return value != 0;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const capacity& lhs,
            const capacity& rhs
        ) noexcept {
            return lhs.value == rhs.value;
        }

        [[nodiscard]] friend constexpr auto operator<=>(
            const capacity& lhs,
            const capacity& rhs
        ) noexcept {
            return lhs.value <=> rhs.value;
        }

        constexpr capacity& operator++() noexcept {
            ++value;
            return *this;
        }

        [[nodiscard]] constexpr capacity operator++(int) noexcept {
            capacity tmp = *this;
            ++value;
            return tmp;
        }

        [[nodiscard]] friend constexpr capacity operator+(
            const capacity& lhs,
            const capacity& rhs
        ) noexcept {
            return capacity(lhs.value + rhs.value);
        }

        constexpr capacity& operator+=(const capacity& other) noexcept {
            value += other.value;
            return *this;
        }

        constexpr capacity& operator--() noexcept {
            --value;
            return *this;
        }

        [[nodiscard]] constexpr capacity operator--(int) noexcept {
            capacity tmp = *this;
            --value;
            return tmp;
        }

        [[nodiscard]] friend constexpr capacity operator-(
            const capacity& lhs,
            const capacity& rhs
        ) noexcept {
            return capacity(lhs.value - rhs.value);
        }

        constexpr capacity& operator-=(const capacity& other) noexcept {
            value -= other.value;
            return *this;
        }

        [[nodiscard]] friend constexpr capacity operator*(
            const capacity& lhs,
            const capacity& rhs
        ) noexcept {
            return capacity(lhs.value * rhs.value);
        }

        constexpr capacity& operator*=(const capacity& other) noexcept {
            value *= other.value;
            return *this;
        }

        [[nodiscard]] friend constexpr capacity operator/(
            const capacity& lhs,
            const capacity& rhs
        ) noexcept {
            return capacity(lhs.value / rhs.value);
        }

        constexpr capacity& operator/=(const capacity& other) noexcept {
            value /= other.value;
            return *this;
        }

        [[nodiscard]] friend constexpr capacity operator%(
            const capacity& lhs,
            const capacity& rhs
        ) noexcept {
            return capacity(lhs.value % rhs.value);
        }

        constexpr capacity& operator%=(const capacity& other) noexcept {
            value %= other.value;
            return *this;
        }

        [[nodiscard]] constexpr capacity operator<<(size_t shift) const noexcept {
            return capacity(value << shift);
        }

        constexpr capacity& operator<<=(size_t shift) noexcept {
            value <<= shift;
            return *this;
        }

        [[nodiscard]] constexpr capacity operator>>(size_t shift) const noexcept {
            return capacity(value >> shift);
        }

        constexpr capacity& operator>>=(size_t shift) noexcept {
            value >>= shift;
            return *this;
        }

        [[nodiscard]] constexpr capacity operator~() const noexcept {
            return capacity(~value);
        }

        [[nodiscard]] constexpr capacity operator&(const capacity& other) const noexcept {
            return capacity(value & other.value);
        }

        constexpr capacity& operator&=(const capacity& other) noexcept {
            value &= other.value;
            return *this;
        }

        [[nodiscard]] constexpr capacity operator|(const capacity& other) const noexcept {
            return capacity(value | other.value);
        }

        constexpr capacity& operator|=(const capacity& other) noexcept {
            value |= other.value;
            return *this;
        }

        [[nodiscard]] constexpr capacity operator^(const capacity& other) const noexcept {
            return capacity(value ^ other.value);
        }

        constexpr capacity& operator^=(const capacity& other) noexcept {
            value ^= other.value;
            return *this;
        }
    };

}


template <meta::unqualified T, impl::capacity<T> N = 0>
struct arena;


namespace impl {

    /// TODO: for windows, use a bitset where every 8 bits represents a chunk of
    /// memory within the arena, consisting of 256 pages (1 MiB on most systems).
    /// The 8-bit value tells how many pages are committed within that chunk, and if
    /// coalescing causes that number to fall to zero, then it will be decommitted to
    /// save space.


    /* Reserve a range of virtual addresses with the given number of bytes at program
    startup.  Returns a void pointer to the start of the address range or nullptr on
    error.

    On unix systems, this will reserve the address space with full read/write
    privileges, and physical pages will be mapped into the region by the OS when they
    are first accessed.  On windows systems, the address space is reserved without any
    privileges, and memory must be explicitly committed before use. */
    [[nodiscard]] inline std::byte* map_address_space(size_t size) noexcept {
        #if defined(_WIN32)
            return reinterpret_cast<std::byte*>(VirtualAlloc(
                reinterpret_cast<LPVOID>(nullptr),
                size,
                MEM_RESERVE,  // does not charge against commit limit
                PAGE_NOACCESS  // no permissions yet
            ));
        #elif defined(__unix__)
            void* ptr = mmap(
                nullptr,
                size,  // no pages are allocated yet
                PROT_READ | PROT_WRITE,  // always read/write
                #if defined(MAP_NORESERVE)
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                #else
                    MAP_PRIVATE | MAP_ANONYMOUS,
                #endif
                -1,  // no file descriptor
                0  // no offset
            );
            return ptr == MAP_FAILED ? nullptr : static_cast<std::byte*>(ptr);
        #else
            return nullptr;  // not supported
        #endif
    }

    /* Release a range of virtual addresses starting at the given pointer with the
    specified number of bytes, returning them to the OS at program shutdown.  This is
    the inverse of `map_address_space()`.  Returns `false` to indicate an error. */
    [[nodiscard]] inline bool unmap_address_space(std::byte* ptr, size_t size) noexcept {
        #if defined(_WIN32)
            return VirtualFree(reinterpret_cast<LPVOID>(ptr), 0, MEM_RELEASE) != 0;
        #elif defined(__unix__)
            return munmap(ptr, size) == 0;
        #else
            return false;  // not supported
        #endif
    }

    /* Commit pages to a portion of a mapped address space starting at `ptr` and
    continuing for `size` bytes.  Physical pages will be mapped into the committed
    region by the OS when they are first accessed.  Returns the same pointer as the
    input if successful, or null to indicate an error.

    On unix systems, this function does nothing, since the entire mapped region is
    always committed, and there is no commit limit to charge against.  On windows
    systems, this will be called in order to expand the committed region as needed,
    increasing syscall overhead, but minimizing commit charge so as not to interfere
    with other processes on the system. */
    [[nodiscard]] inline std::byte* commit_address_space(std::byte* ptr, size_t size) noexcept {
        #if defined(_WIN32)
            return reinterpret_cast<std::byte*>(VirtualAlloc(
                reinterpret_cast<LPVOID>(ptr),
                size,
                MEM_COMMIT,
                PAGE_READWRITE
            ));
        #elif defined(__unix__)
            return ptr;  // nothing to do - always committed
        #else
            return nullptr;  // not supported
        #endif
    }

    /* Decommit pages from a portion of a mapped address space starting at `ptr` and
    continuing for `size` bytes, allowing the OS to reclaim the physical pages
    associated with that region, without affecting the address space itself.  Returns
    `false` to indicate an error.

    On unix systems, this function will be called periodically during the coalesce step
    of the allocator once the total amount of freed memory exceeds a certain threshold.
    On windows systems, this will be called in order to shrink the committed region as
    needed, increasing syscall overhead, but minimizing commit charge so as not to
    interfere with other processes on the system. */
    [[nodiscard]] inline bool decommit_address_space(std::byte* ptr, size_t size) noexcept {
        #if defined(_WIN32)
            return VirtualFree(reinterpret_cast<LPVOID>(ptr), size, MEM_DECOMMIT) != 0;
        #elif defined(__unix__)
            return madvise(ptr, size, MADV_DONTNEED) == 0;
        #else
            return false;  // not supported
        #endif
    }

    /* The overall, global address space is mapped at program startup and unmapped at
    shutdown, writing and reading from a global variable.  `gnu::constructor` and
    `gnu::destructor` with the stated priorities ensure that this mapping is done
    before any other static objects are initialized, and after all other static objects
    have been destroyed, respectively. */
    static std::byte* global_address_space = nullptr;
    static std::atomic<size_t> global_num_address_spaces = 0;
    [[gnu::constructor(101)]] static void alloc_address_space() {
        #if defined(_WIN32)
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            if (si.dwPageSize != PAGE_SIZE) {
                throw MemoryError(std::format(
                    "`BERTRAND_PAGE_SIZE` ({} bytes) does not match system page "
                    "size ({} bytes).  This may cause misaligned virtual address "
                    "spaces and undefined behavior.  Please recompile with "
                    "`BERTRAND_PAGE_SIZE` set to the correct value to silence this "
                    "error",
                    PAGE_SIZE,
                    si.dwPageSize
                ));
            }
        #elif defined(__unix__)
            if (sysconf(_SC_PAGESIZE) != PAGE_SIZE) {
                throw MemoryError(std::format(
                    "`BERTRAND_PAGE_SIZE` ({} bytes) does not match system page "
                    "size ({} bytes).  This may cause misaligned virtual address "
                    "spaces and undefined behavior.  Please recompile with "
                    "`BERTRAND_PAGE_SIZE` set to the correct value to silence this "
                    "error",
                    PAGE_SIZE,
                    sysconf(_SC_PAGESIZE)
                ));
            }
        #endif

        if constexpr (ARENA_SIZE > 0) {
            global_address_space = map_address_space(ARENA_SIZE * NTHREADS);
            if (global_address_space == nullptr) {
                throw MemoryError(std::format(
                    "failed to map global address space for {} arenas of "
                    "{:.2e} MiB each -> {}",
                    NTHREADS,
                    double(ARENA_SIZE) / double(1_MiB),
                    system_err_msg()
                ));
            }
        }
    }
    [[gnu::destructor(65535)]] static void dealloc_address_space() {
        if constexpr (ARENA_SIZE > 0) {
            if (!unmap_address_space(global_address_space, ARENA_SIZE * NTHREADS)) {
                throw MemoryError(std::format(
                    "failed to unmap global address space for {} arenas of "
                    "{:.2e} MiB each -> {}",
                    NTHREADS,
                    double(ARENA_SIZE) / double(1_MiB),
                    system_err_msg()
                ));
            }
        }
    }

    /* Each hardware thread has its own local address space, which it can access
    globally, without locking.  Because the address space is virtual memory, there are
    some differences from a traditional allocator.

    First, because virtual memory is decoupled from physical memory, fragmentation is
    much less of a concern, since unoccupied regions will take up zero physical space.
    In fact, fragmentation is actually an advantage, since it maximizes the probability
    that dynamic allocations can grow in-place without relocating any existing
    elements.  As such, we store the free list as a max heap ordered by overall size,
    and always allocate in the center of the largest available region.

    There is some complication on windows systems, however, since virtual memory must
    be explicitly committed before use, and decommitted when no longer needed.  As
    such, regions will be committed in chunks of 256 pages (1 MiB on most systems) at a
    time in order to minimize syscall overhead, and decommitted whenever their occupied
    count falls to zero.  This process can be avoided on unix because they lack a
    hard commit limit, and only fail when the system runs out of physical memory.



    */
    inline thread_local struct address_space {
        /* The free list is stored contiguously in the first 1/1024th of the address
        space, which is held as a private partition for bookkeeping purposes. */
        static constexpr size_t ADDRESS_SPACE_PARTITION =
            std::min(((ARENA_SIZE + 1023) / 1024), PAGE_SIZE);

        /* The size of the local address space is always set by the build
        configuration. */
        [[nodiscard]] static constexpr size_t size() noexcept {
            return ARENA_SIZE - ADDRESS_SPACE_PARTITION;
        }
        [[nodiscard]] static constexpr ssize_t ssize() noexcept {
            return static_cast<ssize_t>(size());
        }
        [[nodiscard]] static constexpr bool empty() noexcept {
            return size() == 0;
        }

        /* A unique identifier for the current thread, where `0` indicates the main
        thread. */
        size_t id = 0;

        /* A pointer to the start of this thread's local address space.  Note that this
        always comes after the private partition containing the free list. */
        std::byte* ptr = nullptr;

        /* The free list is arranged as a max heap with the largest block at the
        root. */
        struct node {
            std::byte* ptr;  // start of unoccupied memory region
            size_t size;  // length of unoccupied memory region
            node* left = nullptr;  // left subtree
            node* right = nullptr;  // right subtree
        };
        node* freelist = nullptr;
        size_t count = 1;  // number of nodes in the freelist
        size_t occupied = 0;  // number of bytes currently allocated to the user
        size_t committed = ADDRESS_SPACE_PARTITION;  // number of bytes committed

        [[nodiscard]] address_space() :
            id(global_num_address_spaces.fetch_add(1)),
            ptr(ADDRESS_SPACE_PARTITION + (id < NTHREADS ?
                global_address_space + (id * ARENA_SIZE) :
                map_address_space(ARENA_SIZE)
            )),
            freelist(reinterpret_cast<node*>(ptr - ADDRESS_SPACE_PARTITION))
        {
            // map address space for this thread or reuse pre-mapped space
            if (id < NTHREADS) {
                ptr = global_address_space + (id * ARENA_SIZE) + ADDRESS_SPACE_PARTITION;
            } else {
                ptr = map_address_space(ARENA_SIZE);
                if (!ptr) {
                    throw MemoryError(std::format(
                        "failed to map local address space for newly-created "
                        "thread {} ({:.2e} MiB) -> {}",
                        id,
                        double(ARENA_SIZE) / double(1_MiB),
                        system_err_msg()
                    ));
                }
                ptr += ADDRESS_SPACE_PARTITION;
            }

            // commit private partition for freelist
            if (!commit_address_space(
                reinterpret_cast<std::byte*>(freelist),
                ADDRESS_SPACE_PARTITION
            )) {
                throw MemoryError(std::format(
                    "failed to commit private partition for local address space "
                    "of thread {} starting at address {:#x} and ending at "
                    "address {:#x} ({:.2e} MiB) -> {}",
                    id,
                    reinterpret_cast<uintptr_t>(ptr - ADDRESS_SPACE_PARTITION),
                    reinterpret_cast<uintptr_t>(ptr),
                    double(ADDRESS_SPACE_PARTITION) / double(1_MiB),
                    system_err_msg()
                ));
            }

            // push the entire public partition onto the freelist
            std::construct_at(freelist, ptr, size());
        }

        address_space(const address_space&) = delete;
        address_space(address_space&&) = delete;
        address_space& operator=(const address_space&) = delete;
        address_space& operator=(address_space&&) = delete;

        ~address_space() noexcept (false) {
            if (id >= NTHREADS) {
                if (!unmap_address_space(
                    ptr - ADDRESS_SPACE_PARTITION,
                    ARENA_SIZE
                )) {
                    throw MemoryError(std::format(
                        "failed to unmap local address space for thread {} "
                        "starting at address {:#x} and ending at address {:#x} "
                        "({:.2e} MiB) - {}",
                        id,
                        reinterpret_cast<uintptr_t>(ptr - ADDRESS_SPACE_PARTITION),
                        reinterpret_cast<uintptr_t>(ptr - ADDRESS_SPACE_PARTITION + ARENA_SIZE),
                        double(ARENA_SIZE) / double(1_MiB),
                        system_err_msg()
                    ));
                };
            }
        }

    } local_address_space {};




    /* A pool of virtual arenas backing the `arena<T, N>` class.  These arenas
    are allocated upfront at program startup and reused in a manner similar to
    `malloc()`/`free()`, minimizing syscall overhead when apportioning spaces in
    downstream code.  Provides thread-safe access to each arena, and implements a
    simple load-balancing algorithm to ensure that the arenas are evenly utilized. */
    struct address_space {

        /* An individual arena, representing a contiguous virtual address space of
        length `ARENA_SIZE` with a private partition to store an allocator freelist. */
        struct arena {
        private:
            template <meta::unqualified T, impl::capacity<T> N>
            friend struct bertrand::arena;
            friend address_space;

            /* Freelist is held contiguously in the first 1/1024th of the arena.  If
            filled to capacity, this yields an average unoccupied block length of
            ~20 KiB per node in the worst case, which is more than enough capacity for
            general use.  For a 1 GiB arena, assuming 1 page allocations between nodes:

                1 GiB / 1024 = 1 MiB private partition
                1 MiB / sizeof(node) = 1 MiB / 24 B = 43690 node capacity
                1023 MiB - 43690 * 4 KiB = 852.3 MiB empty space
                852.3 MiB / 43690 = ~20.0 KiB average unoccupied block length

            This assumes maximum theoretical fragmentation, which is extremely unlikely
            under ordinary usage, and should never be a problem in practice. */
            static constexpr size_t PARTITION =
                std::min(((ARENA_SIZE + 1023) / 1024), PAGE_SIZE);

            struct node {
                std::byte* ptr;  // start of unoccupied memory region
                size_t length;  // length of unoccupied memory region
                node* next = nullptr;  // next node in the freelist
            };

            node* m_private;  // beginning of private partition
            size_t m_id;  // arena ID in range [0, NTHREADS)
            node* m_head = nullptr;  // proper head of freelist
            node* m_highest = nullptr;  // node with largest address in freelist
            size_t m_size = 0;  // number of nodes in freelist
            size_t m_capacity = 0;  // number of bytes reserved for freelist
            node* m_reuse = nullptr;  // tracks gaps in the node store for reuse
            mutable std::mutex m_mutex;  // mutex for thread-safe access
            std::byte* m_public;  // pointer to the start of public partition
            size_t m_occupied = 0;  // number of bytes used in public partition

            arena(size_t id, std::byte* data) :
                m_id(id),
                m_private(reinterpret_cast<node*>(data)),
                m_public(data + PARTITION)
            {
                std::unique_lock lock(m_mutex);
                if (!push(m_public, capacity())) {
                    throw MemoryError(std::format(
                        "failed to initialize freelist for arena {} starting "
                        "at address {:#x} and ending at address {:#x} ({:.2e} "
                        "MiB) - {}",
                        m_id,
                        reinterpret_cast<uintptr_t>(m_public),
                        reinterpret_cast<uintptr_t>(m_public) + capacity(),
                        double(PAGE_SIZE) / double(1_MiB),
                        system_err_msg()
                    ));
                }
            }

            /* Insert a node into the freelist representing an unoccupied memory
            region starting at `ptr` and continuing for `length` bytes.  This
            will attempt to merge with the previous and next nodes if possible.
            Returns the node that was inserted or merged into, or nullptr if
            an error occurred.  The mutex must be locked for the duration of this
            method. */
            [[nodiscard]] node* push(std::byte* ptr, size_t length) noexcept {
                // find the insertion point for the new node
                node* prev = nullptr;
                node* next = m_head;
                while (next) {
                    if (next->ptr > ptr) {
                        break;
                    }
                    prev = next;
                    next = next->next;
                }

                // check if the new node can be merged with the neighboring nodes
                if (prev && prev->ptr + prev->length == ptr) {
                    prev->length += length;
                    if (next && next->ptr == ptr + length) {
                        prev->length += next->length;
                        prev->next = next->next;
                        next->ptr = nullptr;
                        next->length = 0;
                        next->next = m_reuse;
                        m_reuse = next;
                        --m_size;
                    }
                    return prev;
                } else if (next && next->ptr == ptr + length) {
                    next->ptr = ptr;
                    next->length += length;
                    return next;
                }

                // if the node cannot be merged, we need to allocate a new node
                // and insert it between `prev` and `next`.  Start by checking the
                // `reuse` list to see if we can fill a previously-opened gap in
                // the contiguous region.
                if (m_reuse) {
                    node* curr = m_reuse;
                    m_reuse = m_reuse->next;
                    curr->ptr = ptr;
                    curr->length = length;
                    curr->next = next;
                    if (prev) {
                        prev->next = curr;
                    } else {
                        m_head = curr;
                    }
                    ++m_size;
                    if (curr > m_highest || !m_highest) {
                        m_highest = curr;
                    }
                    return curr;
                }

                // otherwise, all nodes are consolidated, and we may need to
                // allocate additional pages
                if (((m_size + 1) * sizeof(node)) >= m_capacity) {
                    std::byte* pos = reinterpret_cast<std::byte*>(m_private) + m_capacity;
                    if (pos + PAGE_SIZE >= m_public || !commit_address_space(
                        pos,
                        PAGE_SIZE
                    )) {
                        return nullptr;
                    }
                    m_capacity += PAGE_SIZE;
                }

                // allocate from the end of the contiguous region
                node* curr = m_private + m_size;
                curr->ptr = ptr;
                curr->length = length;
                curr->next = next;
                if (prev) {
                    prev->next = curr;
                } else {
                    m_head = curr;
                }
                ++m_size;
                m_highest = curr;  // guaranteed to be highest
                return curr;
            }

            /* Scan the freelist to identify an unoccupied region that can store
            at least `length` bytes, and then adjust the neighboring nodes to
            consider that region occupied.  Returns a pointer to the start of the
            unoccupied region, or nullptr if no such region exists.  The mutex
            must be locked for the duration of this method. */
            [[nodiscard]] std::byte* pop(size_t length) {
                // find the first node large enough to store the requested length
                node* prev = nullptr;
                node* curr = m_head;
                while (curr) {
                    if (curr->length >= length) {
                        std::byte* ptr = curr->ptr;

                        // decrement the node's capacity and increment its pointer
                        // by the requested length
                        curr->length -= length;
                        if (curr->length) {
                            curr->ptr += length;

                        // if the node has no remaining capacity, unlink it from
                        // the freelist and add it to the reuse list
                        } else {
                            if (prev) {
                                prev->next = curr->next;
                            } else {
                                m_head = curr->next;
                            }
                            curr->ptr = nullptr;
                            curr->length = 0;
                            curr->next = m_reuse;
                            m_reuse = curr;
                            --m_size;

                            // if the node was the highest-addressed, then we need
                            // to find the next highest node.  This can be done
                            // quickly by iterating over all addresses to the left
                            // of the node, which are guaranteed to be part of
                            // either the freelist or the reuse list.  We skip over
                            // members of the reuse list, potentially allowing them
                            // to be freed immediately after.
                            if (curr == m_highest) {
                                while (m_highest > m_private && m_highest->ptr == nullptr) {
                                    --m_highest;
                                }

                                // if an error occurs while shrinking the list,
                                // then we need to restore the freelist to its
                                // original state
                                if (!shrink()) {
                                    ++m_size;
                                    curr->ptr = ptr;
                                    curr->length = length;
                                    m_reuse = curr->next;
                                    if (prev) {
                                        curr->next = prev->next;
                                        prev->next = curr;
                                    } else {
                                        curr->next = m_head;
                                        m_head = curr;
                                    }
                                    throw MemoryError(std::format(
                                        "failed to decommit unused pages for "
                                        "freelist in arena {} starting at address "
                                        "{:#x} and ending at address {:#x} ({:.2e} "
                                        "MiB) - {}",
                                        m_id,
                                        reinterpret_cast<uintptr_t>(m_private),
                                        reinterpret_cast<uintptr_t>(m_highest),
                                        double(PAGE_SIZE) / double(1_MiB),
                                        system_err_msg()
                                    ));
                                }
                            }
                        }
                        return ptr;
                    }

                    // advance to next node
                    prev = curr;
                    curr = curr->next;
                }

                // no suitable node was found, so return a null pointer
                return nullptr;
            }

            /* Attempt to decommit unused pages if the load factor drops below a
            given threshold.  A best-faith effort is made to recover from errors
            should the decommit somehow fail. */
            [[nodiscard]] bool shrink() noexcept {
                // if the highest-addressed node is less than 3/4 the allocated
                // capacity, then we can decommit pages to free up physical memory
                size_t span = (m_highest - m_private) * sizeof(node);
                if (m_capacity > (PAGE_SIZE * 4) && span < (m_capacity - m_capacity / 4)) {
                    size_t new_capacity = span + PAGE_SIZE - 1;
                    new_capacity /= PAGE_SIZE;
                    new_capacity *= PAGE_SIZE;

                    // purge the reuse list of any nodes above the new capacity
                    node* prev = nullptr;
                    node* curr = m_reuse;
                    node* limit = m_private + (new_capacity / sizeof(node));
                    node* removed = nullptr;
                    size_t n_removed = 0;
                    while (curr) {
                        if (curr >= limit) {
                            if (prev) {
                                prev->next = curr->next;
                            } else {
                                m_reuse = curr->next;
                            }
                            curr->next = removed;
                            removed = curr;
                            ++n_removed;
                        } else {
                            prev = curr;
                        }
                        curr = curr->next;
                    }

                    // decommit the pages above the new capacity.  If an error
                    // somehow occurs, then make a best-faith effort to restore the
                    // freelist to its original state.  This starts by adding the
                    // removed nodes to the tail of the reuse list (not head), so
                    // that they are not immediately reused, and can potentially
                    // be purged again in the future
                    if (!decommit_address_space(
                        reinterpret_cast<std::byte*>(m_private) + new_capacity,
                        m_capacity - new_capacity
                    )) {
                        prev = nullptr;
                        curr = m_reuse;
                        while (curr) {
                            prev = curr;
                            curr = curr->next;
                        }
                        if (prev) {
                            prev->next = removed;
                        } else {
                            m_reuse = removed;
                        }
                        return false;
                    }
                    m_size -= n_removed;
                    m_capacity = new_capacity;
                }
                return true;
            }

            /* Search for an unoccupied region capable of storing at least `length`
            bytes, and return a pointer to the start of that region.  Adjusts the
            freelist to consider the region occupied, and returns a null pointer if
            no such region exists.  The mutex must be locked when this method is
            called, and will be unlocked when it returns.  Typically, this method is
            invoked immediately after `address_space.acquire()`. */
            [[nodiscard]] std::byte* reserve(size_t length) {
                try {
                    std::byte* ptr = pop(length);
                    if (!ptr) {
                        throw MemoryError(std::format(
                            "failed to locate an unoccupied region of size {} in "
                            "arena {} starting at address {:#x} and ending at address "
                            "{:#x} ({:.2e} MiB)",
                            length,
                            m_id,
                            reinterpret_cast<uintptr_t>(m_public),
                            reinterpret_cast<uintptr_t>(m_public) + capacity(),
                            double(ARENA_SIZE) / double(1_MiB)
                        ));
                    }
                    m_occupied += length;
                    m_mutex.unlock();
                    return ptr;
                } catch (...) {
                    m_mutex.unlock();
                    throw;
                }
            }

            /* Given a pointer to an unoccupied region starting at `ptr` consisting of
            `length` bytes, register the region with the arena's free list so that it
            can be used for future allocations.  Returns false if the freelist could
            not be updated. */
            [[nodiscard]] bool recycle(std::byte* ptr, size_t length) noexcept {
                m_mutex.lock();
                node* chunk = push(ptr, length);
                if (chunk) {
                    m_occupied -= length;
                }
                m_mutex.unlock();
                return chunk;
            }

        public:
            arena(const arena&) = delete;
            arena(arena&&) = delete;
            arena& operator=(const arena&) = delete;
            arena& operator=(arena&&) = delete;

            /* The ID of this arena, which is always an integer in the range
            [0, NTHREADS). */
            [[nodiscard]] size_t id() const noexcept {
                return m_id;
            }

            /* The number of bytes that have been reserved from this arena.  This is
            always less than or equal to `capacity()`. */
            [[nodiscard]] size_t size() const noexcept {
                return m_occupied;
            }

            /* The total amount of virtual memory held by this arena.  This is a
            compile-time constant set by the `BERTRAND_ARENA_SIZE` build flag minus a
            constant amount reserved for the arena's private partition.  More capacity
            allows for a greater number and size of child address spaces, but can
            compete with the rest of the program's virtual address space if set too
            high.  Note that this does not refer to actual physical memory, just a
            range of pointers that are reserved for each arena. */
            [[nodiscard]] static constexpr size_t capacity() noexcept {
                return ARENA_SIZE - PARTITION;
            }

            /* Manually lock the arena's mutex, blocking until it is available and
            returning an RAII lock guard that automatically unlocks it upon
            destruction.  This can potentially deadlock if the mutex is already locked
            by this thread.  As long as the arena is locked, it cannot be used for
            allocation. */
            [[nodiscard]] std::unique_lock<std::mutex> lock() const noexcept {
                return std::unique_lock(m_mutex);
            }

            /* Attempt to lock the arena's mutex without blocking.  If the lock was
            acquired, returns an RAII lock guard that automatically unlocks it upon
            destruction.  Otherwise, returns an empty guard that can later be used for
            deferred locking.  As long as the arena is locked, it cannot be used for
            allocation. */
            [[nodiscard]] std::unique_lock<std::mutex> try_lock() const noexcept {
                return std::unique_lock(m_mutex, std::try_to_lock);
            }
        };

    private:
        template <meta::unqualified T, capacity<T> N>
        friend struct bertrand::arena;

        struct storage;
        friend storage;

        address_space() = default;
        ~address_space() noexcept {
            if (!unmap_address_space(m_data, ARENA_SIZE * NTHREADS)) {
                /// NOTE: destructor is only called at program termination, so
                /// errors here are not critical.  The OS will reclaim memory
                /// anyways unless something is really wrong, so we just log it and
                /// continue.
                std::cerr << system_err_msg() << std::endl;
            }
        }

        /* All arenas are mapped to a single contiguous address range to minimize
        syscall overhead and maximize performance for downstream `mprotect()` calls. */
        std::byte* m_data = [] {
            std::byte* ptr = map_address_space(ARENA_SIZE * NTHREADS);
            if (!ptr) {
                throw MemoryError(std::format(
                    "Failed to map address space for {} arenas starting at "
                    "address {:#x} and ending at address {:#x} ({:.2e} MiB) - {}",
                    ARENA_SIZE * NTHREADS,
                    reinterpret_cast<uintptr_t>(ptr),
                    reinterpret_cast<uintptr_t>(ptr) + ARENA_SIZE * NTHREADS,
                    double(ARENA_SIZE * NTHREADS) / double(1_MiB),
                    system_err_msg()
                ));
            }
            return ptr;
        }();

        /* Arenas are stored in an array where each index is equal to the arena ID. */
        std::array<arena, NTHREADS> m_arenas = []<size_t... Is>(
            std::index_sequence<Is...>,
            std::byte* data
        ) {
            return std::array<arena, NTHREADS>{arena{
                Is,
                data + ARENA_SIZE * Is
            }...};
        }(std::make_index_sequence<NTHREADS>{}, m_data);

        /* Search for an available arena for a requested address space of size
        `capacity`.  A null pointer will be returned if all arenas are full.
        Otherwise, the arena will be returned in a locked state, requiring the caller
        to manually unlock its mutex. */
        [[nodiscard]] arena* acquire(size_t capacity) {
            static thread_local std::mt19937 rng{std::random_device()()};
            static thread_local std::uniform_int_distribution<size_t> dist{
                0,
                NTHREADS - 1
            };

            constexpr size_t max_neighbors = 4;
            constexpr size_t neighbors =
                max_neighbors < NTHREADS ? max_neighbors : NTHREADS;

            // concurrent load balancing is performed stochastically by generating
            // a random index into the arena array and building a neighborhood of
            // the next 4 arenas as a ring buffer sorted by utilization.
            size_t index = 0;
            if constexpr (NTHREADS > max_neighbors) {
                index = dist(rng) % NTHREADS;
            }

            struct ring {
                arena* ptr;
                ring* next = nullptr;
                bool full = false;
            };

            auto ring_store = []<size_t... Is>(
                std::index_sequence<Is...>,
                size_t index,
                arena* arenas
            ) {
                return std::array<ring, neighbors>{
                    ring{&arenas[(index + Is) % NTHREADS]}...
                };
            }(
                std::make_index_sequence<neighbors>{},
                index,
                m_arenas.data()
            );

            ring* head = &ring_store[0];
            ring* tail = &ring_store[0];
            for (size_t i = 1; i < neighbors; ++i) {
                ring& node = ring_store[i];
                if (node.ptr->size() < head->ptr->size()) {
                    node.next = head;
                    head = &node;
                } else if (node.ptr->size() >= tail->ptr->size()) {
                    tail->next = &node;
                    tail = &node;
                } else {
                    ring* prev = head;
                    ring* curr = head->next;
                    while (curr) {
                        if (node.ptr->size() < curr->ptr->size()) {
                            node.next = curr;
                            prev->next = &node;
                            break;
                        }
                        prev = curr;
                        curr = curr->next;
                    }
                }
            }
            tail->next = head;

            // spin around the ring buffer until we find an available arena or
            // all arenas are full
            size_t full_count = 0;
            while (full_count < neighbors) {
                if (!head->full) {
                    if (head->ptr->size() + capacity > ARENA_SIZE) {
                        head->full = true;
                        ++full_count;
                    } else if (head->ptr->m_mutex.try_lock()) {
                        return head->ptr;
                    }
                }
                head = head->next;
            }

            throw MemoryError(std::format(
                "not enough virtual memory for new address space of size "
                "{:.2e} MiB",
                double(capacity) / double(1_MiB)
            ));
        }

    public:
        address_space(const address_space&) = delete;
        address_space(address_space&&) = delete;
        address_space& operator=(const address_space&) = delete;
        address_space& operator=(address_space&&) = delete;

        /* Get a reference to the global address space singleton. */
        [[nodiscard]] static address_space& get() noexcept;

        /* The number of arenas in the global address space.  This is a compile-time
        constant that is set by the `BERTRAND_NTHREADS` build flag, and defaults to 8
        if not specified.  More arenas may reduce contention in multithreaded
        environments. */
        [[nodiscard]] static constexpr size_t size() noexcept {
            return NTHREADS;
        }

        /* Access a specific arena by its ID, which is a sequential integer from 0 to
        `size()`. */
        [[nodiscard]] const arena& operator[](size_t id) const {
            if (id >= NTHREADS) {
                throw IndexError(std::format(
                    "Arena ID {} is out of bounds for global address space with {} "
                    "arenas.",
                    id,
                    NTHREADS
                ));
            }
            return m_arenas[id];
        }

        /* Iterate over the arenas one-by-one to collect usage statistics. */
        [[nodiscard]] const arena* begin() const noexcept {
            if constexpr (NTHREADS == 0) {
                return nullptr;
            } else {
                return &m_arenas[0];
            }
        }

        [[nodiscard]] const arena* end() const noexcept {
            if constexpr (NTHREADS == 0) {
                return nullptr;
            } else {
                return &m_arenas[NTHREADS];
            }
        }
    };

    /* The global address space singleton is stored in an uninitialized private buffer
    that gets initialized first thing before any other global or static constructors
    are run, and last thing after any destructors that may need it.  The
    `address_space::get()` method is the only way to access this instance, and
    another instance cannot be created, destroyed, or unsafely modified in any other
    context. */
    struct address_space::storage {
    private:
        friend address_space;

        inline static bool initialized = false;
        alignas(address_space) inline static unsigned char buffer[
            sizeof(address_space)
        ];

        [[gnu::constructor(101)]] static void init() {
            if constexpr (ARENA_SIZE > 0) {
                if (!initialized) {
                    new (buffer) address_space();
                    initialized = true;
                }
            }
        }

        [[gnu::destructor(65535)]] static void finalize() {
            if constexpr (ARENA_SIZE > 0) {
                if (initialized) {
                    reinterpret_cast<address_space*>(
                        buffer
                    )->~address_space();
                    initialized = false;
                }
            }
        }
    };

    [[nodiscard]] inline address_space& address_space::get() noexcept {
        static_assert(
            ARENA_SIZE > 0,
            "The current system does not support virtual address spaces.  Please set "
            "the `BERTRAND_ARENA_SIZE`, `BERTRAND_NTHREADS`, and `BERTRAND_PAGE_SIZE` "
            "build flags to positive values to enable this feature, or condition this "
            "code path on the state of the constexpr `ARENA_SIZE` variable to provide "
            "an alternative."
        );
        return *reinterpret_cast<address_space*>(storage::buffer);
    }

}


namespace meta {

    template <typename Alloc>
    concept arena = inherits<Alloc, impl::arena_tag>;

    template <typename Alloc, typename T>
    concept arena_for = arena<Alloc> && ::std::same_as<typename unqualify<Alloc>::type, T>;

}


/// TODO: once P3074 becomes available, stack-based arenas should just work as
/// expected.


/* A contiguous region of reserved addresses holding uninitialized instances of type
`T`.  If `T` is `void`, then the arena will be interpreted as raw bytes, which can be
used to store arbitrary data.

This specialization refers to a dynamic arena whose size is given as a constructor
argument (as either a raw number of instances or a number of bytes with an appropriate
unit suffix), which is backed by either the heap or a virtual memory allocator
depending on whether it is used at compile time or runtime.  If used at compile time,
then the arena will act like a normal dynamic array of a certain size.  If used at
runtime, then the arena will reserve virtual addresses from Bertrand's built-in
allocators, but not commit physical memory until it is needed.  If the current system
(such as an embedded platform without a MMU) does not support virtual memory, then the
heap will be used at runtime instead, and a compilation warning will be issued
directing the user to disable virtual memory in their build configuration.

Virtual memory arenas are always preferred over heap-based alternatives due to their
stability, laziness, and integration with Bertrand's threading model.  Because they
map physical memory to addresses only as needed, users can safely request more memory
than they actually need (possibly even more than is physically present on the system)
without incurring an immediate cost.  This also means that existing elements are
guaranteed not to be relocated as the arena grows until it reaches its maximum reserved
size.  Even then, there is a high likelihood that the region can simply be extended
in-place, since fragmentation is much less of a concern compared to traditional heap
allocation.  Finally, since a thread-local virtual memory allocator is built into all
of Bertrand's core hardware threads, they may be accessed lock-free without contention,
using a large global allocator as a fallback if the thread-local pool is unavailable or
exhausted.

The only downsides of virtual memory arenas are that they cannot be `constexpr`, and
always allocate memory in page-aligned chunks (usually 4 KiB or larger), which can
lead to wasted space if many small allocations are made.  Bertrand mitigates these by
falling back to traditional heap arrays as noted above, as well as special-casing most
container literals to use stack-based arenas where appropriate.

In both the heap and virtual memory cases, the reserved memory will be deallocated when
the arena is destroyed, following RAII principles.  Note that no destructors will be
called for any constructed elements; it is the user's responsibility to manage the
lifetime of the objects stored within the arena, and ensure they are properly destroyed
before the arena itself. */
template <meta::unqualified T, impl::capacity<T> N>
struct arena {
    using type = T;

    /* Identifies static (stack-based) vs dynamic (heap or virtual memory-based)
    arenas. */
    [[nodiscard]] static constexpr bool dynamic() noexcept { return true; }

private:
    using Alloc = std::allocator<type>;

public:
    type* ptr = nullptr;
    size_t len = 0;
    /// TODO: add a pointer to the virtual memory allocator this was allocated from,
    /// or just always use the thread-local address space (which may be global)?

    /* Default-constructing a dynamic arena does not allocate any memory and sets the
    size to zero. */
    [[nodiscard]] constexpr arena() noexcept = default;

    /* Passing a capacity to the constructor allocates the indicated amount of memory
    from either the heap (at compile time) or a virtual memory arena (at runtime).  A
    `MemoryError` may be thrown if the allocation fails. */
    [[nodiscard]] constexpr arena(impl::capacity<type> n) : len(n.value) {
        if consteval {
            ptr = Alloc{}.allocate(len);
            if (ptr == nullptr) {
                throw MemoryError("failed to allocate memory for arena");
            }
        } else {
            /// TODO: allocate from virtual memory
        }
    }

    /* Arenas are never copyable. */
    [[nodiscard]] constexpr arena(const arena&) = delete;
    constexpr arena& operator=(const arena&) = delete;

    /* Dynamic arenas are movable, transferring ownership of the underlying memory. */
    [[nodiscard]] constexpr arena(arena&& other) noexcept :
        ptr(other.ptr),
        len(other.len)
    {
        other.ptr = nullptr;
        other.len = 0;
    }
    constexpr arena& operator=(arena&& other) noexcept {
        if (this != &other) {
            if (ptr != nullptr) {
                if consteval {
                    Alloc{}.deallocate(ptr, len);
                } else {
                    /// TODO: deallocate virtual memory
                }
            }
            ptr = other.ptr;
            len = other.len;
            other.ptr = nullptr;
            other.len = 0;
        }
        return *this;
    }

    /* Destroying a dynamic arena deallocates the underlying memory according to
    RAII. */
    constexpr ~arena() noexcept {
        if (ptr != nullptr) {
            if consteval {
                Alloc{}.deallocate(ptr, len);
                ptr = nullptr;
                len = 0;
            } else {
                /// TODO: deallocate virtual memory
            }
        }
    }

    /* Returns true if the arena owns at least one element. */
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return len > 0; }

    /* Returns true if the arena is empty, meaning it does not own any memory. */
    [[nodiscard]] constexpr bool empty() const noexcept { return len == 0; }

    /* Returns the number of elements in the arena as an unsigned integer. */
    [[nodiscard]] constexpr size_t size() const noexcept { return len; }

    /* Returns the number of elements in the arena as a signed integer. */
    [[nodiscard]] constexpr ssize_t ssize() const noexcept { return ssize_t(len); }

    /* Retrieve a mutable pointer to the beginning of the underlying memory. */
    [[nodiscard]] constexpr type* data() noexcept { return ptr; }

    /* Retrieve a read-only pointer to the beginning of the underlying memory. */
    [[nodiscard]] constexpr const type* data() const noexcept {
        return static_cast<const type*>(ptr);
    }

    /* Construct the element at the specified index of the arena and return a reference
    to the newly constructed instance.  A different type may be provided as a template
    parameter, in which case the element will be reinterpreted as that type before
    constructing it.  It is up to the user to ensure that this does not lead to
    undefined behavior. */
    template <meta::unqualified V = type, typename... A>
    constexpr V& construct(size_t i, A&&... args) &
        noexcept (meta::nothrow::constructible_from<V, A...>)
        requires (meta::constructible_from<V, A...>)
    {
        if constexpr (meta::explicitly_convertible_to<type*, V*>) {
            return *std::construct_at(
                static_cast<V*>(data() + i),
                std::forward<A>(args)...
            );
        } else {
            return *std::construct_at(
                reinterpret_cast<V*>(data() + i),
                std::forward<A>(args)...
            );
        }
    }
    template <meta::unqualified V = type, typename... A>
    constexpr V&& construct(size_t i, A&&... args) &&
        noexcept (meta::nothrow::constructible_from<V, A...>)
        requires (meta::constructible_from<V, A...>)
    {
        if constexpr (meta::explicitly_convertible_to<type*, V*>) {
            return std::move(*std::construct_at(
                static_cast<V*>(data() + i),
                std::forward<A>(args)...
            ));
        } else {
            return std::move(*std::construct_at(
                reinterpret_cast<V*>(data() + i),
                std::forward<A>(args)...
            ));
        }
    }

    /* Destroy the element at the specified index of the arena. */
    template <meta::unqualified V = type>
    constexpr void destroy(size_t i)
        noexcept (meta::nothrow::destructible<V>)
        requires (meta::destructible<V>)
    {
        std::destroy_at(data() + i);
    }

    /* Attempt to reserve additional memory for the arena without reallocating.  The
    argument `n` indicates the new desired size of the arena (independent of the
    current size).  Returns `true` if the requested memory was successfully reserved
    (meaning no current elements need to be relocated), or `false` if it requires
    the allocation of a new arena and the relocation of existing elements, which is up
    to the user to handle. */
    [[nodiscard]] constexpr bool reserve(size_t n) noexcept {
        if consteval {
            return n <= size();
        } else {
            /// TODO: attempt to extend current region without reallocating
        }
    }

    /* Perfectly forward the first element of the arena. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) front(this Self&& self) noexcept {
        if constexpr (meta::lvalue<Self>) {
            return (self.ptr[0]);
        } else {
            return (std::move(self.ptr[0]));
        }
    }

    /* Perfectly forward the last element of the arena. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) back(this Self&& self) noexcept {
        if constexpr (meta::lvalue<Self>) {
            return (self.ptr[self.len - 1]);
        } else {
            return (std::move(self.ptr[self.len - 1]));
        }
    }

    /* Perfectly forward an element of the arena identified by index. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, size_t index) noexcept {
        if constexpr (meta::lvalue<Self>) {
            return (self.ptr[index]);
        } else {
            return (std::move(self.ptr[index]));
        }
    }

    /* Get an iterator to the beginning of the arena. */
    template <typename Self>
    [[nodiscard]] constexpr auto begin(this Self&& self) noexcept {
        if constexpr (meta::lvalue<Self>) {
            return self.data();
        } else {
            return std::move_iterator(self.data());
        }
    }

    /* Get an iterator to the end of the arena. */
    template <typename Self>
    [[nodiscard]] constexpr auto end(this Self&& self) noexcept {
        if constexpr (meta::lvalue<Self>) {
            return self.data() + self.len;
        } else {
            return std::move_iterator(self.data() + self.len);
        }
    }

    /* Get a reverse iterator to the beginning of the arena. */
    template <typename Self>
    [[nodiscard]] constexpr auto rbegin(this Self&& self) noexcept {
        return std::make_reverse_iterator(std::forward<Self>(self).end());
    }

    /* Get a reverse iterator to the end of the arena. */
    template <typename Self>
    [[nodiscard]] constexpr auto rend(this Self&& self) noexcept {
        return std::make_reverse_iterator(std::forward<Self>(self).begin());
    }
};


/* A static arena that allocates a fixed-size array on the stack.

This specialization is chosen when the templated capacity is greater than zero, in
which case an uninitialized array of that size is allocated as a member of the struct.
Note that such arenas are not copy or move constructible/assignable, since the arena is
not aware of which elements have been initialized or not.  It is the user's
responsibility to manage the lifetime of the elements stored within the arena, and if
copies or moves are required, they must be performed by requesting another arena and
filling it manually. */
template <meta::unqualified T, impl::capacity<T> N> requires (N > 0)
struct arena<T, N> {
    using type = T;

    /* Identifies static (stack-based) vs dynamic (heap or virtual memory-based)
    arenas. */
    [[nodiscard]] static constexpr bool dynamic() noexcept { return false; }

    /* Returns true if the arena owns at least one element. */
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return true; }

    /* Returns true if the arena is empty, meaning it does not own any memory. */
    [[nodiscard]] static constexpr bool empty() noexcept { return false; }

    /* Returns the number of elements in the arena as an unsigned integer. */
    [[nodiscard]] static constexpr size_t size() noexcept { return N.value; }

    /* Returns the number of elements in the arena as a signed integer. */
    [[nodiscard]] static constexpr ssize_t ssize() noexcept { return ssize_t(N.value); }

private:
    union storage {
        T data[size()];
        constexpr storage() noexcept requires (meta::trivially_constructible<T>) = default;
        constexpr storage() noexcept requires (!meta::trivially_constructible<T>) {}
        constexpr ~storage() noexcept requires (meta::trivially_destructible<T>) = default;
        constexpr ~storage() noexcept requires (!meta::trivially_destructible<T>) {};
    };

public:
    storage m_storage;

    /* Default-constructing a static arena reserves stack space, but does not
    initialize any of the elements. */
    [[nodiscard]] constexpr arena() noexcept = default;

    /* Static arenas cannot be copied or moved, since they are not aware of which
    elements are constructed and which are not. */
    constexpr arena(const arena&) = delete;
    constexpr arena(arena&&) = delete;
    constexpr arena& operator=(const arena&) = delete;
    constexpr arena& operator=(arena&&) = delete;

    /* Retrieve a mutable pointer to the beginning of the underlying memory. */
    [[nodiscard]] constexpr type* data() noexcept { return m_storage.data; }

    /* Retrieve a read-only pointer to the beginning of the underlying memory. */
    [[nodiscard]] constexpr const type* data() const noexcept {
        return static_cast<const type*>(m_storage.data);
    }

    /* Construct the element at the specified index of the arena and return a reference
    to it. */
    template <meta::unqualified V = type, typename... A>
    constexpr V& construct(size_t i, A&&... args) &
        noexcept (meta::nothrow::constructible_from<V, A...>)
        requires (meta::constructible_from<V, A...>)
    {
        if constexpr (meta::explicitly_convertible_to<type*, V*>) {
            return *std::construct_at(
                static_cast<V*>(data() + i),
                std::forward<A>(args)...
            );
        } else {
            return *std::construct_at(
                reinterpret_cast<V*>(data() + i),
                std::forward<A>(args)...
            );
        }
    }
    template <meta::unqualified V = type, typename... A>
    constexpr V&& construct(size_t i, A&&... args) &&
        noexcept (meta::nothrow::constructible_from<V, A...>)
        requires (meta::constructible_from<V, A...>)
    {
        if constexpr (meta::explicitly_convertible_to<type*, V*>) {
            return std::move(*std::construct_at(
                static_cast<V*>(data() + i),
                std::forward<A>(args)...
            ));
        } else {
            return std::move(*std::construct_at(
                reinterpret_cast<V*>(data() + i),
                std::forward<A>(args)...
            ));
        }
    }

    /* Destroy the element at the specified index of the arena. */
    constexpr void destroy(size_t i)
        noexcept (meta::nothrow::destructible<T>)
        requires (meta::destructible<T>)
    {
        std::destroy_at(data() + i);
    }

    /* Attempt to reserve additional memory for the arena without reallocating.  The
    argument `n` indicates the new desired size of the arena (independent of the
    current size).  Returns `true` if the requested memory was successfully reserved,
    or `false` if it requires relocation, which is up to the user to handle. */
    [[nodiscard]] static constexpr bool reserve(size_t n) noexcept {
        return n <= size();
    }

    /* Perfectly forward the first element of the arena. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) front(this Self&& self) noexcept {
        return (std::forward<Self>(self).m_storage.data[0]);
    }

    /* Perfectly forward the last element of the arena. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) back(this Self&& self) noexcept {
        return (std::forward<Self>(self).m_storage.data[size() - 1]);
    }

    /* Perfectly forward an element of the arena identified by index. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self, size_t index) noexcept {
        return (std::forward<Self>(self).m_storage.data[index]);
    }

    /* Get an iterator to the beginning of the arena. */
    template <typename Self>
    [[nodiscard]] constexpr auto begin(this Self&& self) noexcept {
        if constexpr (meta::lvalue<Self>) {
            return self.data();
        } else {
            return std::move_iterator(self.data());
        }
    }

    /* Get an iterator to the end of the arena. */
    template <typename Self>
    [[nodiscard]] constexpr auto end(this Self&& self) noexcept {
        if constexpr (meta::lvalue<Self>) {
            return self.data() + size();
        } else {
            return std::move_iterator(self.data() + size());
        }
    }

    /* Get a reverse iterator to the beginning of the arena. */
    template <typename Self>
    [[nodiscard]] constexpr auto rbegin(this Self&& self) noexcept {
        return std::make_reverse_iterator(std::forward<Self>(self).end());
    }

    /* Get a reverse iterator to the end of the arena. */
    template <typename Self>
    [[nodiscard]] constexpr auto rend(this Self&& self) noexcept {
        return std::make_reverse_iterator(std::forward<Self>(self).begin());
    }
};





static_assert([] {
    arena<int> arr{5};
    if (arr.size() != 5) return false;
    arr.construct(0, 10);
    if (arr[0] != 10) return false;
    arr.destroy(0);

    return true;
}());










// template <meta::unqualified T, impl::capacity<T> N>
// struct arena : impl::arena_tag {
//     using size_type = size_t;
//     using capacity_type = impl::capacity<T>;
//     using difference_type = ptrdiff_t;
//     using type = T;
//     using value_type = std::conditional_t<meta::is_void<T>, std::byte, T>;
//     using reference = value_type&;
//     using const_reference = const value_type&;
//     using pointer = value_type*;
//     using const_pointer = const value_type*;
//     using iterator = pointer;
//     using const_iterator = const_pointer;
//     using reverse_iterator = std::reverse_iterator<iterator>;
//     using const_reverse_iterator = std::reverse_iterator<const_iterator>;

//     /* Indicates whether this address space falls into the small space optimization.
//     Such spaces are allocated on the heap rather than using page-aligned virtual
//     memory. */
//     static constexpr bool SMALL = false;

// private:

//     static constexpr capacity_type GUARD_SIZE =
//         (N.item_size + PAGE_SIZE - 1) / PAGE_SIZE * PAGE_SIZE;
//     static constexpr capacity_type TOTAL_SIZE = GUARD_SIZE + N.page_align() + GUARD_SIZE;

// public:

//     /* Default constructor.  Reserves address space, but does not commit any memory. */
//     [[nodiscard]] arena() :
//         m_arena(impl::address_space::get().acquire(TOTAL_SIZE)),
//         m_data(m_arena->reserve(TOTAL_SIZE) + GUARD_SIZE)
//     {}

//     arena(const arena&) = delete;
//     arena& operator=(const arena&) = delete;

//     [[nodiscard]] arena(arena&& other) noexcept :
//         m_arena(other.m_arena), m_data(other.m_data)
//     {
//         other.m_arena = nullptr;
//         other.m_data = nullptr;
//     }

//     [[maybe_unused]] arena& operator=(arena&& other) noexcept(!DEBUG) {
//         if (this != &other) {
//             if (m_data) {
//                 std::byte* p = m_data;
//                 if (!impl::purge_address_space(p, nbytes())) {
//                     throw MemoryError(std::format(
//                         "failed to decommit pages from virtual address space starting "
//                         "at address {:#x} and ending at address {:#x} ({:.2e} MiB) - {}",
//                         reinterpret_cast<uintptr_t>(m_data),
//                         reinterpret_cast<uintptr_t>(m_data) + nbytes(),
//                         double(nbytes()) / double(1_MiB),
//                         impl::system_err_msg()
//                     ));
//                 }
//                 if (!m_arena->recycle(p - GUARD_SIZE, TOTAL_SIZE)) {
//                     throw MemoryError(std::format(
//                         "failed to commit additional pages to freelist for arena {} "
//                         "starting at address {:#x} and ending at address {:#x} "
//                         "({:.2e} MiB) - {}",
//                         m_arena->id(),
//                         reinterpret_cast<uintptr_t>(m_data),
//                         reinterpret_cast<uintptr_t>(m_data) + nbytes(),
//                         double(nbytes()) / double(1_MiB),
//                         impl::system_err_msg()
//                     ));
//                 }
//             }
//             m_arena = other.m_arena;
//             m_data = other.m_data;
//             other.m_arena = nullptr;
//             other.m_data = nullptr;
//         }
//         return *this;
//     }

//     /* Unreserve the address space upon destruction, returning the virtual capacity to
//     the arena's freelist and allowing the operating system to reclaim physical pages
//     for future allocations. */
//     ~arena() noexcept(false) {
//         m_arena = nullptr;
//         if (m_data) {
//             std::byte* p = m_data;
//             if (!impl::purge_address_space(p, nbytes())) {
//                 throw MemoryError(std::format(
//                     "failed to decommit pages from virtual address space starting "
//                     "at address {:#x} and ending at address {:#x} ({:.2e} MiB) - {}",
//                     reinterpret_cast<uintptr_t>(m_data),
//                     reinterpret_cast<uintptr_t>(m_data) + nbytes(),
//                     double(nbytes()) / double(1_MiB),
//                     impl::system_err_msg()
//                 ));
//             }
//             if (!m_arena->recycle(p - GUARD_SIZE, TOTAL_SIZE)) {
//                 throw MemoryError(std::format(
//                     "failed to commit additional pages to freelist for arena {} "
//                     "starting at address {:#x} and ending at address {:#x} "
//                     "({:.2e} MiB) - {}",
//                     m_arena->id(),
//                     reinterpret_cast<uintptr_t>(m_data),
//                     reinterpret_cast<uintptr_t>(m_data) + nbytes(),
//                     double(nbytes()) / double(1_MiB),
//                     impl::system_err_msg()
//                 ));
//             }
//             m_data = nullptr;
//         }
//     }

//     /* True if the address space owns a virtual memory handle.  False otherwise.
//     Currently, this only occurs after an address space has been moved from, leaving it
//     in an empty state. */
//     [[nodiscard]] explicit operator bool() const noexcept { return m_data; }

//     /* Return the maximum size of the virtual address space in units of `T`.  If `T` is
//     void, then this refers to the total size (in bytes) of the address space itself.
//     Otherwise, it is equivalent to `nbytes()` divided by the aligned size of `T`. */
//     [[nodiscard]] static constexpr size_type size() noexcept { return N.count(); }

//     /* Return the maximum size of the address space in bytes.  If `T` is void, then
//     this is always equivalent to `N`.  Otherwise, the result is adjusted downwards to
//     be a multiple of the aligned size of type `T`. */
//     [[nodiscard]] static constexpr size_type nbytes() noexcept { return N; }

//     /* Get a pointer to the beginning of the reserved address space.  If `T` is void,
//     then the pointer will be returned as `std::byte*`. */
//     [[nodiscard]] pointer data() noexcept {
//         return reinterpret_cast<pointer>(m_data);
//     }
//     [[nodiscard]] const_pointer data() const noexcept {
//         return reinterpret_cast<const_pointer>(m_data);
//     }

//     /* Iterate over the reserved addresses.  If `T` is void, then each element is
//     represented as a `std::byte`. */
//     [[nodiscard]] iterator begin() noexcept { return data(); }
//     [[nodiscard]] const_iterator begin() const noexcept { return data(); }
//     [[nodiscard]] const_iterator cbegin() const noexcept { return data();}
//     [[nodiscard]] iterator end() noexcept { return begin() + size(); }
//     [[nodiscard]] const_iterator end() const noexcept { return begin() + size(); }
//     [[nodiscard]] const_iterator cend() const noexcept { return begin() + size(); }
//     [[nodiscard]] reverse_iterator rbegin() noexcept { return {end()}; }
//     [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return {end()}; }
//     [[nodiscard]] const_reverse_iterator crbegin() const noexcept { return {end()}; }
//     [[nodiscard]] reverse_iterator rend() noexcept { return {begin()}; }
//     [[nodiscard]] const_reverse_iterator rend() const noexcept { return {begin()}; }
//     [[nodiscard]] const_reverse_iterator crend() const noexcept { return {begin()}; }

//     /* Manually commit physical memory to the address space.  If `T` is not void and
//     the offset and/or length have no units, then they will be multiplied by the aligned
//     size of `T` to determine the actual offsets for the relevant syscall.  If the
//     length is empty, then no memory will be committed.  The result is a pointer to the
//     start of the committed region, and a `MemoryError` may be thrown if committing the
//     memory resulted in an OS error, with the original message being forwarded to the
//     user.

//     Individual arena implementations are expected to wrap this method to make the
//     interface more convenient.  This simply abstracts the low-level OS hooks to make
//     them cross-platform. */
//     [[maybe_unused, gnu::malloc]] pointer allocate(
//         capacity_type offset,
//         capacity_type length
//     ) {
//         if constexpr (DEBUG) {
//             if (offset + length > nbytes()) {
//                 throw MemoryError(std::format(
//                     "attempted to commit memory at offset {} with length {}, "
//                     "which exceeds the size of the virtual address space starting "
//                     "at address {:#x} and ending at address {:#x} ({:.2e} MiB)",
//                     offset,
//                     length,
//                     reinterpret_cast<uintptr_t>(m_data),
//                     reinterpret_cast<uintptr_t>(m_data) + nbytes(),
//                     double(nbytes()) / double(1_MiB)
//                 ));
//             }
//         }
//         if (!length) {
//             return reinterpret_cast<pointer>(m_data + offset);
//         }
//         pointer result = reinterpret_cast<pointer>(impl::commit_address_space(
//             m_data,
//             offset,
//             length
//         ));
//         if (result == nullptr) {
//             throw MemoryError(std::format(
//                 "failed to commit pages to virtual address space starting at "
//                 "address {:#x} and ending at address {:#x} ({:.2e} MiB) - {}",
//                 reinterpret_cast<uintptr_t>(m_data),
//                 reinterpret_cast<uintptr_t>(m_data) + nbytes(),
//                 double(nbytes()) / double(1_MiB),
//                 impl::system_err_msg()
//             ));
//         }
//         return result;
//     }

//     /* Manually release physical memory from the address space.  If `T` is not void and
//     the offset and/or length have no units, then they will be multiplied by the aligned
//     size of `T` to determine the actual offsets for the relevant syscall.  If the
//     length is empty, then no memory will be decommitted.  A `MemoryError` may be thrown
//     if decommitting the memory resulted in an OS error, with the original message being
//     forwarded to the user.

//     Individual arena implementations are expected to wrap this method to make the
//     interface more convenient.  This simply abstracts the low-level OS hooks to make
//     them cross-platform.  Note that this does not remove addresses from the space
//     itself, which is always handled automatically by the destructor. */
//     void deallocate(capacity_type offset, capacity_type length) {
//         if constexpr (DEBUG) {
//             if (offset + length > N) {
//                 throw MemoryError(std::format(
//                     "attempted to decommit memory at offset {} with length {}, "
//                     "which exceeds the size of the virtual address space starting "
//                     "at address {:#x} and ending at address {:#x} ({:.2e} MiB)",
//                     offset,
//                     length,
//                     reinterpret_cast<uintptr_t>(m_data),
//                     reinterpret_cast<uintptr_t>(m_data) + nbytes(),
//                     double(nbytes()) / double(1_MiB)
//                 ));
//             }
//         }
//         if (!length) {
//             return;
//         }
//         if (!impl::decommit_address_space(
//             m_data,
//             offset,
//             length
//         )) {
//             throw MemoryError(std::format(
//                 "failed to decommit pages from virtual address space starting at "
//                 "address {:#x} and ending at address {:#x} ({:.2e} MiB) - {}",
//                 reinterpret_cast<uintptr_t>(m_data),
//                 reinterpret_cast<uintptr_t>(m_data) + nbytes(),
//                 double(nbytes()) / double(1_MiB),
//                 impl::system_err_msg()
//             ));
//         };
//     }

//     /* Construct a value that was allocated from this address space using placement
//     new.  Propagates any errors that emanate from the constructor for `T`. */
//     template <typename... Args>
//         requires (meta::not_void<T> && meta::constructible_from<T, Args...>)
//     void construct(pointer p, Args&&... args)
//         noexcept(!DEBUG && noexcept(new (p) T(std::forward<Args>(args)...)))
//     {
//         if constexpr (DEBUG) {
//             if (p < begin() || p >= end()) {
//                 throw MemoryError(std::format(
//                     "pointer at address {:#x} was not allocated from the virtual "
//                     "address space starting at address {:#x} and ending at "
//                     "address {:#x} ({:.2e} MiB)",
//                     reinterpret_cast<uintptr_t>(p),
//                     reinterpret_cast<uintptr_t>(m_data),
//                     reinterpret_cast<uintptr_t>(m_data) + nbytes(),
//                     double(nbytes()) / double(1_MiB)
//                 ));
//             }
//         }
//         new (p) T(std::forward<Args>(args)...);
//     }

//     /* Destroy a value that was allocated from this address space by calling its
//     destructor in-place.  Note that this does not deallocate the underlying memory,
//     which only occurs when the address space is destroyed, or when memory is explicitly
//     decommitted using the `deallocate()` method.  Any errors emanating from the
//     destructor for `T` will be propagated. */
//     void destroy(pointer p) noexcept(!DEBUG && meta::nothrow::destructible<T>) {
//         if constexpr (DEBUG) {
//             if (p < begin() || p >= end()) {
//                 throw MemoryError(std::format(
//                     "pointer at address {:#x} was not allocated from the virtual "
//                     "address space starting at address {:#x} and ending at "
//                     "address {:#x} ({:.2e} MiB)",
//                     reinterpret_cast<uintptr_t>(p),
//                     reinterpret_cast<uintptr_t>(m_data),
//                     reinterpret_cast<uintptr_t>(m_data) + nbytes(),
//                     double(nbytes()) / double(1_MiB)
//                 ));
//             }
//         }
//         if constexpr (!meta::trivially_destructible<T>) {
//             p->~T();
//         }
//         std::memset(p, 0, N.item_size);
//     }

// private:
//     typename impl::address_space::arena* m_arena;
//     std::byte* m_data;
// };


// /* Small space optimization for `arena<T, N>`, where `N * sizeof(T)` is less than one
// full page in memory.

// This bypasses the virtual memory arenas and allocates the address space on the heap
// using the system's `calloc()` and `free()`.  Since virtual address spaces are always
// aligned to the nearest page boundary, spaces smaller than that can use the heap to
// avoid wasting memory and reduce contention on the global arenas, which are only used
// for multi-page allocations.  This tends to reduce the total number of syscalls and
// improve performance for small, transient data structures, without compromising any of
// the pointer stability guarantees of the larger address spaces.  Both cases expose the
// same interface, so the user does not need to worry about which one is being used. */
// template <meta::unqualified T, impl::capacity<T> N>
//     requires (N.page_align() <= PAGE_SIZE)
// struct arena<T, N> : impl::arena_tag {
//     using size_type = size_t;
//     using capacity_type = impl::capacity<T>;
//     using difference_type = ptrdiff_t;
//     using type = T;
//     using value_type = std::conditional_t<meta::is_void<T>, std::byte, T>;
//     using reference = value_type&;
//     using const_reference = const value_type&;
//     using pointer = value_type*;
//     using const_pointer = const value_type*;
//     using iterator = pointer;
//     using const_iterator = const_pointer;
//     using reverse_iterator = std::reverse_iterator<iterator>;
//     using const_reverse_iterator = const std::reverse_iterator<const_iterator>;

//     /* Indicates whether this address space falls into the small space optimization.
//     Such spaces cannot be copied or moved without downstream, implementation-defined
//     logic. */
//     static constexpr bool SMALL = true;

//     /* Default constructor.  `calloc()`s uninitialized storage equal to the size of the
//     space, initialized to zero. */
//     [[nodiscard]] arena() : m_data(calloc(N)) {
//         if (!m_data) {
//             throw MemoryError(std::format(
//                 "failed to allocate {} bytes for address space starting at "
//                 "address {:#x} and ending at address {:#x} ({:.2e} MiB)",
//                 nbytes(),
//                 reinterpret_cast<uintptr_t>(m_data),
//                 reinterpret_cast<uintptr_t>(m_data) + nbytes(),
//                 double(nbytes()) / double(1_MiB)
//             ));
//         }
//     }

//     arena(const arena& other) = delete;
//     arena& operator=(const arena& other) = delete;

//     [[nodiscard]] arena(arena&& other) noexcept :
//         m_data(other.m_data)
//     {
//         other.m_data = nullptr;
//     }

//     [[maybe_unused]] arena& operator=(arena&& other) noexcept {
//         if (this != &other) {
//             if (m_data) {
//                 free(m_data);
//             }
//             m_data = other.m_data;
//             other.m_data = nullptr;
//         }
//         return *this;
//     }

//     /* `free()` the address space upon destruction. */
//     ~arena() {
//         if (m_data) {
//             free(m_data);
//             m_data = nullptr;
//         }
//     }

//     /* True if the address space owns a virtual memory handle.  False otherwise.
//     Currently, this only occurs after an address space has been moved from, leaving it
//     in an empty state. */
//     [[nodiscard]] explicit operator bool() const noexcept { return m_data; }

//     /* Return the maximum size of the physical space in units of `T`.  If `T` is void,
//     then this refers to the total size (in bytes) of the physical space itself. */
//     [[nodiscard]] static constexpr size_type size() noexcept { return N.count(); }

//     /* Return the maximum size of the physical space in bytes.  If `T` is void, then
//     this is equivalent to `size()`. */
//     [[nodiscard]] static constexpr size_type nbytes() noexcept { return N; }

//     /* Get a pointer to the beginning of the reserved physical space.  If `T` is void,
//     then the pointer will be returned as `std::byte*`. */
//     [[nodiscard]] pointer data() noexcept {
//         return reinterpret_cast<pointer>(m_data);
//     }
//     [[nodiscard]] const_pointer data() const noexcept {
//         return reinterpret_cast<const_pointer>(m_data);
//     }

//     /* Iterate over the reserved addresses.  If `T` is void, then each element is
//     represented as a `std::byte`. */
//     [[nodiscard]] iterator begin() noexcept { return data(); }
//     [[nodiscard]] const_iterator begin() const noexcept { return data(); }
//     [[nodiscard]] const_iterator cbegin() const noexcept { return data(); }
//     [[nodiscard]] iterator end() noexcept { return begin() + size(); }
//     [[nodiscard]] const_iterator end() const noexcept { return begin() + size(); }
//     [[nodiscard]] const_iterator cend() const noexcept { return begin() + size(); }
//     [[nodiscard]] reverse_iterator rbegin() noexcept { return {end()}; }
//     [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return {end()}; }
//     [[nodiscard]] const_reverse_iterator crbegin() const noexcept { return {end()}; }
//     [[nodiscard]] reverse_iterator rend() noexcept { return {begin()}; }
//     [[nodiscard]] const_reverse_iterator rend() const noexcept { return {begin()}; }
//     [[nodiscard]] const_reverse_iterator crend() const noexcept { return {begin()}; }

//     /* Return a pointer to the beginning of a contiguous region of the physical space.
//     If `T` is not void and the offset and/or length have no units, then they will be
//     multiplied by the aligned size of `T` to determine the actual offsets.  The result
//     is a pointer to the start of the allocated region. */
//     [[maybe_unused, gnu::malloc]] pointer allocate(
//         capacity_type offset,
//         capacity_type length
//     ) noexcept(!DEBUG) {
//         if constexpr (DEBUG) {
//             if (offset + length > nbytes()) {
//                 throw MemoryError(std::format(
//                     "attempted to commit memory at offset {} with length {}, "
//                     "which exceeds the size of the physical space starting "
//                     "at address {:#x} and ending at address {:#x} ({:.2e} MiB)",
//                     offset,
//                     length,
//                     reinterpret_cast<uintptr_t>(m_data),
//                     reinterpret_cast<uintptr_t>(m_data) + nbytes(),
//                     double(nbytes()) / double(1_MiB)
//                 ));
//             }
//         }
//         return reinterpret_cast<pointer>(m_data + offset);
//     }

//     /* Zero out the physical memory associated with the given range.  If `T` is not void and
//     the offset and/or length have no units, then they will be multiplied by the aligned
//     size of `T` to determine the actual offsets. */
//     void deallocate(size_type offset, size_type length) noexcept(!DEBUG) {
//         if constexpr (DEBUG) {
//             if (offset + length > N) {
//                 throw MemoryError(std::format(
//                     "attempted to decommit memory at offset {} with length {}, "
//                     "which exceeds the size of the physical space starting "
//                     "at address {:#x} and ending at address {:#x} ({:.2e} MiB)",
//                     offset,
//                     length,
//                     reinterpret_cast<uintptr_t>(m_data),
//                     reinterpret_cast<uintptr_t>(m_data) + nbytes(),
//                     double(nbytes()) / double(1_MiB)
//                 ));
//             }
//         }
//     }

//     /* Construct a value that was allocated from this physical space using placement
//     new.  Propagates any errors that emanate from the constructor for `T`. */
//     template <typename... Args>
//         requires (meta::not_void<T> && meta::constructible_from<T, Args...>)
//     void construct(pointer p, Args&&... args)
//         noexcept(!DEBUG && noexcept(new (p) T(std::forward<Args>(args)...)))
//     {
//         if constexpr (DEBUG) {
//             if (p < begin() || p >= end()) {
//                 throw MemoryError(std::format(
//                     "pointer at address {:#x} was not allocated from the physical "
//                     "space starting at address {:#x} and ending at "
//                     "address {:#x} ({:.2e} MiB)",
//                     reinterpret_cast<uintptr_t>(p),
//                     reinterpret_cast<uintptr_t>(m_data),
//                     reinterpret_cast<uintptr_t>(m_data) + nbytes(),
//                     double(nbytes()) / double(1_MiB)
//                 ));
//             }
//         }
//         new (p) T(std::forward<Args>(args)...);
//     }

//     /* Destroy a value that was allocated from this physical space by calling its
//     destructor in-place.  Note that this does not deallocate the memory itself, which
//     only occurs when the physical space is destroyed.  Any errors emanating from the
//     destructor for `T` will be propagated. */
//     void destroy(pointer p) noexcept(!DEBUG && meta::nothrow::destructible<T>) {
//         if constexpr (DEBUG) {
//             if (p < begin() || p >= end()) {
//                 throw MemoryError(std::format(
//                     "pointer at address {:#x} was not allocated from the physical "
//                     "space starting at address {:#x} and ending at "
//                     "address {:#x} ({:.2e} MiB)",
//                     reinterpret_cast<uintptr_t>(p),
//                     reinterpret_cast<uintptr_t>(m_data),
//                     reinterpret_cast<uintptr_t>(m_data) + nbytes(),
//                     double(nbytes()) / double(1_MiB)
//                 ));
//             }
//         }
//         if constexpr (!meta::trivially_destructible<T>) {
//             p->~T();
//         }
//         std::memset(p, 0, N.item_size);
//     }

// private:
//     std::byte* m_data;
// };


}  // namespace bertrand


#endif  // BERTRAND_ALLOCATE_H
