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


/// TODO: document these flags, and possibly come up with better names, like
/// BERTRAND_VMEM_PER_THREAD, BERTRAND_COALESCE_AFTER, 


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
    constexpr size_t ARENA_SIZE = 32_GiB;
#endif
static_assert(
    ARENA_SIZE % PAGE_SIZE == 0,
    "arena size must be a multiple of the page size"
);
static_assert(
    ARENA_SIZE / PAGE_SIZE <= std::numeric_limits<uint32_t>::max(),
    "arena size too large to fully index using 32-bit integers"
);
static_assert(
    ARENA_SIZE == 0 || ARENA_SIZE >= (PAGE_SIZE * 2),
    "arena must contain at least two pages of memory to store a private and "
    "public partition"
);


#ifdef BERTRAND_ALLOCATE_COALESCE_AFTER
    constexpr size_t COALESCE_THRESHOLD = BERTRAND_ALLOCATE_COALESCE_AFTER;
#else
    constexpr size_t COALESCE_THRESHOLD = 16_MiB;
#endif


#ifdef BERTRAND_ALLOCATE_RADIX_AFTER
    constexpr size_t INSERTION_THRESHOLD = BERTRAND_ALLOCATE_RADIX_AFTER;
#else
    constexpr size_t INSERTION_THRESHOLD = 64;
#endif


#ifdef BERTRAND_ALLOCATE_DEBUG
    constexpr bool DEBUG_ALLOCATOR = true;
#else
    constexpr bool DEBUG_ALLOCATOR = false;
#endif


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

    static NoneType check_page_size = [] {
        #if WINDOWS
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
        #elif UNIX
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
        return None;
    }();

    /* Reserve a range of virtual addresses with the given number of bytes at program
    startup.  Returns a void pointer to the start of the address range or nullptr on
    error.

    On unix systems, this will reserve the address space with full read/write
    privileges, and physical pages will be mapped into the region by the OS when they
    are first accessed.  On windows systems, the address space is reserved without any
    privileges, and memory must be explicitly committed before use. */
    [[nodiscard]] inline void* map_address_space(size_t size) noexcept {
        #if WINDOWS
            return reinterpret_cast<void*>(VirtualAlloc(
                reinterpret_cast<LPVOID>(nullptr),
                size,
                MEM_RESERVE,  // does not charge against commit limit
                PAGE_NOACCESS  // no permissions yet
            ));
        #elif UNIX
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
            return ptr == MAP_FAILED ? nullptr : ptr;
        #else
            return nullptr;  // not supported
        #endif
    }

    /* Release a range of virtual addresses starting at the given pointer with the
    specified number of bytes, returning them to the OS at program shutdown.  This is
    the inverse of `map_address_space()`.  Returns `false` to indicate an error. */
    [[nodiscard]] inline bool unmap_address_space(void* ptr, size_t size) noexcept {
        #if WINDOWS
            return VirtualFree(reinterpret_cast<LPVOID>(ptr), 0, MEM_RELEASE) != 0;
        #elif UNIX
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
    [[nodiscard]] inline void* commit_address_space(void* ptr, size_t size) noexcept {
        #if WINDOWS
            return reinterpret_cast<void*>(VirtualAlloc(
                reinterpret_cast<LPVOID>(ptr),
                size,
                MEM_COMMIT,
                PAGE_READWRITE
            ));
        #elif UNIX
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
    [[nodiscard]] inline bool decommit_address_space(void* ptr, size_t size) noexcept {
        #if WINDOWS
            return VirtualFree(reinterpret_cast<LPVOID>(ptr), size, MEM_DECOMMIT) != 0;
        #elif UNIX
            return madvise(ptr, size, MADV_DONTNEED) == 0;
        #else
            return false;  // not supported
        #endif
    }

    /* Round an allocation size (in bytes) up to the nearest full page, and then return
    it as a 32-bit page count.  Multiplying the result by the page size gives the
    total size in bytes. */
    [[nodiscard]] constexpr uint32_t page_count(size_t size) noexcept {
        return size / PAGE_SIZE + (size % PAGE_SIZE != 0);
    }

    /* Round an allocation size (in bytes) up to the nearest multiple of the system's
    pointer alignment (usually 8 bytes).  Returns the rounded size in bytes. */
    [[nodiscard]] constexpr size_t align_native(size_t nbytes) {
        return nbytes / alignof(void*) + (nbytes % alignof(void*) != 0);
    }

    /// TODO: long-term, what I should do is add a couple slab allocators within the
    /// public partition to handle small allocations up to a couple pages in size
    /// immediately, without TLB pressure or fragmentation


    /* Each hardware thread has its own local address space, which it can access
    globally, without locking.  Because the address space hands out virtual memory,
    there are some differences from traditional allocators.

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
    hard commit limit, and only fail when the system runs out of physical memory. */
    inline thread_local struct address_space {
    private:
        template <meta::unqualified T, impl::capacity<T> N>
        friend struct bertrand::arena;

        /* Because we asserted that the address space can be fully indexed using 32-bit
        integers, and we always reserve at least one page as a private partition, we
        can safely reuse the maximum 32-bit unsigned integer as a sentinel value. */
        static constexpr uint32_t NIL = std::numeric_limits<uint32_t>::max();

        /* Due to the windows commit limit, care must be taken not to commit too much
        virtual memory at once (not a problem on unix).  In order to address this, each
        address space will include a ledger tracking chunks of 255 pages at a time
        (~1 MiB on most systems), where the numeric value indicates how many pages of
        that chunk have been allocated to the user.  When the value drops to zero, the
        entire chunk will be decommitted as part of the coalesce step.  This marginally
        increases syscall overhead during allocation, but is the only realistic way to
        work around windows commit costs while maintaining the benefits of virtual
        memory.  Note that the ledger spans the entire address space, including the
        private partition, so that the node storage may be chunked on the same basis. */
        static constexpr uint32_t CHUNK_LEDGER = !WINDOWS ? 0 :
            ARENA_SIZE / (PAGE_SIZE * std::numeric_limits<uint8_t>::max()) +
            (ARENA_SIZE % (PAGE_SIZE * std::numeric_limits<uint8_t>::max()) != 0);

        /* In order to reduce syscall overhead, empty nodes are not immediately
        decommitted and returned to the free list.  Instead, they are added to a batch
        array that grows until a certain threshold is reached, after which the batched
        nodes are sorted by offset, coalesced with adjacent free blocks, and
        decommitted en masse.  Only after this are the batched nodes moved back into
        the free list.

        The sorting algorithm is an LSD radix sort with 8-bit digits and OneSweep-style
        loop fusion, which requires two temporary buffers to store the digit counts and
        prefix sums.  Additionally, insertion sort will be used as an optimization if 
        the batch size is below a (tunable) threshold.  */
        static constexpr uint32_t COALESCE_BUFFER = page_count(COALESCE_THRESHOLD);
        static constexpr uint32_t RADIX_POWER = 256;
        static constexpr uint32_t RADIX_LOG2 = 8;
        static constexpr uint32_t RADIX_MASK = RADIX_POWER - 1;
        static constexpr uint32_t RADIX_LAST = std::numeric_limits<uint32_t>::digits - RADIX_LOG2;

        /* Because the radix sort buffers are persistent, they must be zeroed before
        every invocation. */
        constexpr void reset_radix_counts() noexcept {
            if consteval {
                for (uint32_t i = 0; i < RADIX_POWER; ++i) radix_count[i] = 0;
            } else {
                std::memset(radix_count, 0, RADIX_POWER * sizeof(uint32_t));
            }
        }

        /* Nodes use an intrusive heap + treap data structure that compresses to just
        24 bytes.  The nodes are stored in a stable, private partition array starting
        at a byte offset equal to `BASE_SIZE`.  In order to save space and promote
        cache locality, the max-heap heap is stored as an overlapping array using the
        `heap_array` member, which should only ever be accessed by the `heap()` helper
        method for clarity.  Lastly, the free list reuses the `right` member on
        uninitialized nodes as a pointer to the next node in the singly-linked list. */
        struct node {
            uint32_t offset;  // start of memory region, as a page offset from base ptr
            uint32_t size;  // number of pages in memory region
            uint32_t left;  // left BST child index
            uint32_t right;  // right BST child index or index of next node in singly-linked list
            uint32_t heap;  // index into the heap array for this node
            uint32_t heap_array;  // overlapping heap array

            /* Deterministically compute a priority value for this node by passing its
            offset into a splitmix32-style finalizer.  The current implementation is
            bijective, meaning that it returns perfect hashes for every possible
            input, allowing us to drop collision pathways in treap rotations.  If this
            ever changes in the future, then the collision pathway will need to be
            restored. */
            [[nodiscard]] constexpr uint32_t priority() const noexcept {
                uint32_t x = offset;
                x ^= x >> 16;
                x *= 0x7feb352dU;  // odd => invertible mod 2^32
                x ^= x >> 15;
                x *= 0x846ca68bU;  // odd => invertible mod 2^32
                x ^= x >> 16;
                return x;
            }

            /* Get an offset to the end of the memory region, for merging purposes. */
            [[nodiscard]] constexpr uint32_t end() const noexcept {
                return offset + size;
            }
        };

        /* Look up an index in the overlapping heap array without the confusing
        `nodes[pos].heap` syntax. */
        [[nodiscard]] constexpr uint32_t& heap(uint32_t pos) noexcept {
            return nodes[pos].heap_array;
        }
        [[nodiscard]] constexpr const uint32_t& heap(uint32_t pos) const noexcept {
            return nodes[pos].heap_array;
        }

        /* The node list and max heap are stored contiguously in a region large enough
        to hold one page per node in the worst case.  Allocations will never be more
        fine-grained than this due to strict page alignment, so we will never run out
        of nodes unless the entire address space is full.  Because each node only
        consumes 24 bytes on average (plus an extra byte per 255 nodes for a windows
        chunk ledger), and pages are generally 4 KiB in the worst case, this private
        partition will require only ~0.6% of the overall address space, and will be
        able to represent up to 16 TiB of address space per thread.  This becomes more
        efficient as the page size increases. */
        static constexpr struct Size {
            static constexpr size_t BASE = 
                align_native(CHUNK_LEDGER * sizeof(uint8_t)) +
                2 * align_native(COALESCE_BUFFER * sizeof(uint32_t)) +
                2 * align_native(RADIX_POWER * sizeof(uint32_t));
            uint32_t partition;
            uint32_t pub;
        } SIZE = [] -> Size {
            // initial upper bound -> x * (page + node) <= ARENA_SIZE
            uint32_t lo = 0;
            uint32_t hi = ARENA_SIZE / (PAGE_SIZE + sizeof(node));

            // binary search for maximum N where partition(N) + (N * page) <= ARENA_SIZE
            uint32_t partition = 0;
            while (lo < hi) {
                uint32_t mid = (hi + lo + 1) / 2;
                partition = page_count(Size::BASE + mid * sizeof(node));
                if (
                    partition <= ARENA_SIZE / PAGE_SIZE &&
                    mid <= ARENA_SIZE / PAGE_SIZE - partition
                ) {
                    lo = mid;
                } else {
                    hi = mid - 1;
                }
            }
            return {partition, lo};
        }();

        bool initialized = false;  // whether the address space has been initialized
        bool teardown = false;  // whether the address space's destructor has been called
        uint32_t next_id = 0;  // high-water mark for node indices with empty free list
        uint32_t occupied = 0;  // number of pages currently allocated to the user
        uint32_t heap_size = 0;  // number of active nodes (excluding batch and free list)
        uint32_t root = NIL;  // index to root of coalesce tree
        uint32_t free = NIL;  // index to first uninitialized node in storage or NIL
        uint32_t batch_count = 0;  // number of nodes in batch list
        uint32_t batched = 0;  // total number of pages stored in batch list
        uint8_t* chunks = nullptr;  // windows commit chunk ledger
        uint32_t* radix_count = nullptr;  // temporary array for radix sort counts
        uint32_t* radix_sum = nullptr;  // temporary array for radix sort prefix sums
        uint32_t* batch = nullptr;  // batch list
        uint32_t* batch_sort = nullptr;  // temporary array for radix-sorting batch list
        node* nodes = nullptr;  // node storage + overlapping heap
        void* ptr = nullptr;  // start of this thread's local address space

        /* Initialize the address space on first use.  This is called in the
        constructor for `arena<T, N>`, ensuring proper initialization order, even for
        objects with static storage duration. */
        void acquire() {
            if (initialized) {
                return;
            }

            // map address space for this thread
            ptr = map_address_space((SIZE.partition + SIZE.pub) * PAGE_SIZE);
            if (!ptr) {
                throw MemoryError(std::format(
                    "failed to map local address space for newly-created thread "
                    "({:.3e} MiB) -> {}",
                    size_in_mib(),
                    system_err_msg()
                ));
            }

            // commit windows chunk ledger, batch array, and batch sort array
            size_t commit = page_count(Size::BASE);
            if (!commit_address_space(ptr, commit * PAGE_SIZE)) {
                teardown = true;
                release();
                throw MemoryError(std::format(
                    "failed to commit chunk ledger for local address space "
                    "starting at address {:#x} and ending at address {:#x} "
                    "({:.3e} MiB) -> {}",
                    reinterpret_cast<uintptr_t>(ptr),
                    reinterpret_cast<uintptr_t>(ptr) + commit * PAGE_SIZE,
                    double(commit * PAGE_SIZE) / double(1_MiB),
                    system_err_msg()
                ));
            }

            // calculate pointers to private data structures
            size_t i = 0;
            chunks = reinterpret_cast<uint8_t*>(static_cast<std::byte*>(ptr) + i);
            i += align_native(CHUNK_LEDGER * sizeof(uint8_t));
            radix_count = reinterpret_cast<uint32_t*>(static_cast<std::byte*>(ptr) + i);
            i += align_native(RADIX_POWER * sizeof(uint32_t));
            radix_sum = reinterpret_cast<uint32_t*>(static_cast<std::byte*>(ptr) + i);
            i += align_native(RADIX_POWER * sizeof(uint32_t));
            batch = reinterpret_cast<uint32_t*>(static_cast<std::byte*>(ptr) + i);
            i += align_native(COALESCE_BUFFER * sizeof(uint32_t));
            batch_sort = reinterpret_cast<uint32_t*>(static_cast<std::byte*>(ptr) + i);
            i += align_native(COALESCE_BUFFER * sizeof(uint32_t));
            nodes = reinterpret_cast<node*>(static_cast<std::byte*>(ptr) + i);
            ptr = static_cast<std::byte*>(ptr) + SIZE.partition * PAGE_SIZE;  // public partition

            // initialize chunk ledger if present
            if constexpr (WINDOWS) {
                for (uint32_t i = 0; commit > 0; ++i) {
                    if (commit > std::numeric_limits<uint8_t>::max()) {
                        chunks[i] = std::numeric_limits<uint8_t>::max();
                        commit -= std::numeric_limits<uint8_t>::max();
                    } else {
                        chunks[i] = static_cast<uint8_t>(commit);
                        break;
                    }
                }
            }

            // initialize single free node covering entire public partition
            nodes->offset = 0;  // start of public partition
            nodes->size = SIZE.pub;  // whole public partition
            nodes->left = NIL;  // no children
            nodes->right = NIL;  // no children
            nodes->heap = 0;  // only node in heap
            heap(0) = 0;  // only heap entry
            heap_size = 1;  // only active node
            root = 0;  // root of coalesce tree
            next_id = 1;  // node index high-water mark
            initialized = true;  // set initialized signal
        }

        /* Release the address space on last use.  This is a no-op unless `teardown` is
        set to `true` (indicating that the address space's destructor has been called)
        and the address space is empty.  It will be called in the destructor for the
        address space itself as well as `arena<T, N>` to ensure proper destruction
        order, even for objects with static storage duration, where destructor order is
        not guaranteed. */
        void release() {
            if (teardown && occupied == 0) {
                if (!unmap_address_space(
                    start_ptr(),
                    (SIZE.partition + SIZE.pub) * PAGE_SIZE
                )) {
                    throw MemoryError(std::format(
                        "failed to unmap local address space starting at address "
                        "{:#x} and ending at address {:#x} ({:.3e} MiB) -> {}",
                        reinterpret_cast<uintptr_t>(start_ptr()),
                        reinterpret_cast<uintptr_t>(end_ptr()),
                        size_in_mib(),
                        system_err_msg()
                    ));
                }
            }
        }

        /* Return a pointer to the beginning of the private partition. */
        [[nodiscard]] constexpr void* start_ptr() const noexcept {
            return static_cast<std::byte*>(ptr) - SIZE.partition * PAGE_SIZE;
        }

        /* Return a pointer to the end of the public partition. */
        [[nodiscard]] constexpr void* end_ptr() const noexcept {
            return static_cast<std::byte*>(ptr) + SIZE.pub * PAGE_SIZE;
        }

        /* Report the total size of the private partition specifically, in MiB, for
        diagnostic purposes. */
        [[nodiscard]] constexpr double private_size_in_mib() const noexcept {
            return double(SIZE.partition * PAGE_SIZE) / double(1_MiB);
        }

        /* Report the total size of the public partition specifically, in MiB, for
        diagnostic purposes. */
        [[nodiscard]] constexpr double public_size_in_mib() const noexcept {
            return double(SIZE.pub * PAGE_SIZE) / double(1_MiB);
        }

        /* Report the total size of the address space in MiB, for diagnostic
        purposes. */
        [[nodiscard]] constexpr double size_in_mib() const noexcept {
            return double((SIZE.partition + SIZE.pub) * PAGE_SIZE) / double(1_MiB);
        }

        /* Report the current utilization of the address space in MiB, for diagnostic
        purposes.  Note that this does not count the physical ram consumed by the
        allocations, just their overall footprint in virtual address space. */
        [[nodiscard]] constexpr double utilization_in_mib() const noexcept {
            return double(occupied * PAGE_SIZE) / double(1_MiB);
        }

        /* Report the current utilization of the address space as a percentage, for
        diagnostic purposes. */
        [[nodiscard]] constexpr double percent_utilization() const noexcept {
            return (double(occupied) / double(SIZE.pub)) * 100.0;
        }

        /// TODO: if BERTRAND_DEBUG_ALLOCATOR is set, add extra assertions to
        /// validate the heap and treap invariants after every modification, and
        /// whatever other consistency checks the coalesce step might need.

        /* Swap two heap indices, accounting for intrusive indices into the node
        array. */
        constexpr void heap_swap(uint32_t lhs, uint32_t rhs) noexcept {
            uint32_t a = heap(lhs);
            uint32_t b = heap(rhs);
            heap(lhs) = b;
            heap(rhs) = a;
            nodes[a].heap = rhs;
            nodes[b].heap = lhs;
        }

        /* Sift the current heap index down the heap, comparing it with its children to
        restore the max heap invariant.  Returns the new heap index after sifting. */
        constexpr uint32_t heapify_down(uint32_t pos) noexcept {
            while (true) {
                uint32_t left = (pos * 2) + 1;
                uint32_t right = left + 1;
                if (
                    left < heap_size &&
                    nodes[heap(left)].size > nodes[heap(pos)].size
                ) {
                    heap_swap(pos, left);
                    pos = left;
                } else if (
                    right < heap_size &&
                    nodes[heap(right)].size > nodes[heap(pos)].size
                ) {
                    heap_swap(pos, right);
                    pos = right;
                } else {
                    break;
                }
            }
            return pos;
        }

        /* Sift the current heap index up the heap, comparing it with its parents to
        restore the max heap invariant.  Returns the new heap index after sifting. */
        constexpr uint32_t heapify_up(uint32_t pos) noexcept {
            while (pos > 0) {
                uint32_t parent = (pos - 1) / 2;
                if (nodes[heap(pos)].size > nodes[heap(parent)].size) {
                    heap_swap(pos, parent);
                    pos = parent;
                } else {
                    break;
                }
            }
            return pos;
        }

        /* Insert the current node into the heap, maintaining the max heap
        invariant. */
        constexpr void heap_insert(uint32_t id) noexcept {
            heap(heap_size) = id;
            nodes[id].heap = heap_size;
            heapify_up(heap_size);
            ++heap_size;
        }

        /* Remove the current node from the heap, maintaining the max heap
        invariant. */
        constexpr void heap_erase(uint32_t id) noexcept {
            uint32_t pos = nodes[id].heap;
            uint32_t last = heap(heap_size - 1);
            heap(pos) = last;
            nodes[last].heap = pos;
            --heap_size;
            nodes[id].heap = NIL;  // mark as removed
            if (pos < heap_size) {  // fix heap if not removing last node
                heapify_down(pos);
                heapify_up(pos);
            }
        }

        /* Restore the heap invariant for the current node, assuming it is still in
        the heap. */
        constexpr void heap_fix(uint32_t id) noexcept {
            uint32_t pos = nodes[id].heap;
            heapify_down(pos);
            heapify_up(pos);
        }

        /* Iteratively update the links of `root` to insert a node with a given `key`,
        then set that node's children to maintain sorted order.  This is done without
        an explicit stack, by modifying the links in-place. */
        constexpr void treap_split(
            uint32_t root,
            uint32_t key,
            uint32_t& left,
            uint32_t& right
        ) noexcept {
            left = NIL;
            right = NIL;
            uint32_t left_tail = NIL;
            uint32_t right_tail = NIL;

            // split using in-place tree rotations
            while (root != NIL) {
                if (nodes[root].offset < key) {
                    // append root to the right spine of left subtree
                    uint32_t next = nodes[root].right;
                    if (left_tail == NIL) {
                        left = root;
                    } else {
                        nodes[left_tail].right = root;
                    }
                    left_tail = root;
                    root = next;
                } else {
                    // append root to the left spine of right subtree
                    uint32_t next = nodes[root].left;
                    if (right_tail == NIL) {
                        right = root;
                    } else {
                        nodes[right_tail].left = root;
                    }
                    right_tail = root;
                    root = next;
                }
            }

            // terminate spines
            if (left_tail != NIL) {
                nodes[left_tail].right = NIL;
            }
            if (right_tail != NIL) {
                nodes[right_tail].left = NIL;
            }
        }

        /* Iteratively insert a node into the treap, sorting by offset.  The return
        value is the new root of the treap, and may be identical to the previous root.
        This is done without an explicit stack via a simple pointer, which modifies
        links in-place. */
        [[nodiscard]] constexpr uint32_t treap_insert(uint32_t root, uint32_t id) noexcept {
            uint32_t key = nodes[id].offset;
            uint32_t priority = nodes[id].priority();

            nodes[id].left = NIL;
            nodes[id].right = NIL;

            // walk using a pointer-to-link so subtree roots can be replaced in-place
            uint32_t* link = &root;
            while (*link != NIL) {
                uint32_t curr = *link;

                // min-heap by priority.  Note that since the priority function is
                // bijective modulo 2^32 (and therefore produces perfect hashes), we
                // can remove the collision path here as a micro-optimization.  If the
                // priority function ever loses the bijective property, then this must
                // be restored.
                if (uint32_t p = nodes[curr].priority(); priority < p
                    // || (priority == p && nodes[curr].offset < key)
                ) {
                    treap_split(
                        curr,
                        key,
                        nodes[id].left,
                        nodes[id].right
                    );
                    *link = id;
                    return root;
                }

                // otherwise, descend by BST key
                if (key < nodes[curr].offset) {
                    link = &nodes[curr].left;
                } else {
                    link = &nodes[curr].right;
                }
            }

            // fell off the tree - insert as leaf
            *link = id;
            return root;
        }

        /* Iteratively search for a key in the treap and remove it while maintaining
        sorted order.  Note that the key must compare equal to exactly one entry in the
        treap.  The return value is the new root of the treap, and may be identical to
        the previous root.  Note this is done without an explicit stack via a simple
        pointer, which modifies links in-place by rotating down the tree. */
        [[nodiscard]] constexpr uint32_t treap_erase(uint32_t root, uint32_t id) noexcept {
            uint32_t key = nodes[id].offset;

            // find node by key
            uint32_t* link = &root;
            while (true) {
                if (*link == NIL) {
                    return root;
                }
                uint32_t curr = *link;
                if (nodes[curr].offset == key) {
                    break;
                }
                link = (key < nodes[curr].offset) ? &nodes[curr].left : &nodes[curr].right;
            }

            // iteratively rotate down until node has <= 1 child, then splice
            while (true) {
                uint32_t curr = *link;
                uint32_t left = nodes[curr].left;
                uint32_t right = nodes[curr].right;
                if (left == NIL) {
                    *link = right;
                    nodes[curr].left = NIL;
                    nodes[curr].right = NIL;
                    return root;
                } else if (right == NIL) {
                    *link = left;
                    nodes[curr].left = NIL;
                    nodes[curr].right = NIL;
                    return root;
                }

                // bubble up the child with smaller priority (min heap).  Note that
                // just like `treap_insert()`, we can remove the collision path here as
                // a micro-optimization due to the bijective property of the priority
                // function.  If this ever changes, the collision path must be restored.
                if (uint32_t p_left = nodes[left].priority(), p_right = nodes[right].priority();
                    p_left < p_right
                    // || (p_left == p_right && nodes[left].offset < nodes[right].offset
                ) {
                    // rotate right at *link
                    uint32_t x = *link;
                    uint32_t y = nodes[x].left;
                    nodes[x].left = nodes[y].right;
                    nodes[y].right = x;
                    *link = y;

                    // target moved to right child
                    link = &nodes[y].right;
                } else {
                    // rotate left at *link
                    uint32_t x = *link;
                    uint32_t y = nodes[x].right;
                    nodes[x].right = nodes[y].left;
                    nodes[y].left = x;
                    *link = y;

                    // target moved to left child
                    link = &nodes[y].left;
                }
            }
        }

        /* Find the free block immediately preceding the given key in the treap. */
        [[nodiscard]] constexpr uint32_t treap_prev(uint32_t root, uint32_t key) const noexcept {
            uint32_t best = NIL;
            while (root != NIL) {
                node& n = nodes[root];
                if (n.offset < key) {
                    best = root;
                    root = n.right;
                } else {
                    root = n.left;
                }
            }
            return best;
        }

        /* Find the free block immediately following the given key in the treap. */
        [[nodiscard]] constexpr uint32_t treap_next(uint32_t root, uint32_t key) const noexcept {
            uint32_t best = NIL;
            while (root != NIL) {
                node& n = nodes[root];
                if (n.offset >= key) {
                    best = root;
                    root = n.left;
                } else {
                    root = n.right;
                }
            }
            return best;
        }

        /* Get a node id from the free list or end of storage.  Note that this does
        not insert the node into either the heap or treap. */
        [[nodiscard]] constexpr uint32_t get_node() noexcept {
            if (free != NIL) {
                uint32_t n = free;
                free = nodes[n].right;  // advance free list (possibly NIL)
                return n;
            }
            return next_id++;
        }

        /* Apply insertion sort to sub-arrays with size less than `INSERTION_THRESHOLD`
        to avoid constant factors in radix sort. */
        constexpr void insertion_sort() noexcept {
            for (uint32_t i = 1; i < batch_count; ++i) {
                if (nodes[batch[i]].offset < nodes[batch[i - 1]].offset) {
                    uint32_t tmp = batch[i];
                    batch[i] = batch[i - 1];
                    uint32_t j = i - 1;
                    while (j > 0 && nodes[tmp].offset < nodes[batch[j - 1]].offset) {
                        batch[j] = batch[j - 1];
                        --j;
                    }
                    batch[j] = tmp;
                }
            }
        }

        /* Radix sort the batch list by offset, updating it in-place.  This algorithm
        incorporates an innovation from OneSweep: A Faster Least Significant Digit
        Radix Sort for GPUs (Andy Adinets and Duane Merrill, 2022).

            https://arxiv.org/abs/2206.01784

        This effectively combines counting for the next pass with scatter for the
        current pass, reducing the total number of iterations from `O(2d(n + k))` to
        `O((d + 1)(n + k))`, where `d` is the number of digits per key (4 in this
        case), `n` is the number of elements, and `k` is the number of bins (256 for
        8-bit digits). */
        constexpr void radix_sort() noexcept {
            if (batch_count < 2) {
                return;
            }
            if (batch_count <= INSERTION_THRESHOLD) {
                insertion_sort();
                return;
            }
            reset_radix_counts();

            // initial counting to determine bin sizes/offsets for first digit
            for (uint32_t i = 0; i < batch_count; ++i) {
                ++radix_count[nodes[batch[i]].offset & RADIX_MASK];
            }

            // compute prefix sums
            uint32_t prefix = 0;
            for (uint32_t i = 0; i < RADIX_POWER; ++i) {
                uint32_t n = radix_count[i];
                radix_sum[i] = prefix;  // starting index for this bin
                prefix += n;
            }

            // sort ids into bins by offset digit value, least significant first
            for (uint32_t shift = 0; shift < RADIX_LAST; shift += RADIX_LOG2) {
                reset_radix_counts();

                // scatter combined with counting for next pass
                for (uint32_t i = 0; i < batch_count; ++i) {
                    uint32_t key = nodes[batch[i]].offset >> shift;
                    batch_sort[radix_sum[key & RADIX_MASK]++] = batch[i];
                    ++radix_count[(key >> RADIX_LOG2) & RADIX_MASK];
                }

                // recompute prefix sums for next pass
                prefix = 0;
                for (uint32_t i = 0; i < RADIX_POWER; ++i) {
                    uint32_t n = radix_count[i];
                    radix_sum[i] = prefix;  // starting index for next pass
                    prefix += n;
                }

                // swap buffers
                std::swap(batch, batch_sort);
            }

            // final pass can avoid simultaneous counting
            for (uint32_t i = 0; i < batch_count; ++i) {
                uint32_t key = nodes[batch[i]].offset;
                batch_sort[radix_sum[(key >> RADIX_LAST) & RADIX_MASK]++] = batch[i];
            }

            // ensure the sorted data is kept in the original batch array
            std::swap(batch, batch_sort);
        }

        /* Extend an initial run to the right, merging with adjacent nodes from either
        the batch or the treap.  Any merged nodes will be appended to the free list,
        and those that originate from the treap will be removed from both the heap and
        treap.  If no adjacent nodes are found, then the run is complete. */
        [[nodiscard]] constexpr bool cascade(uint32_t run, uint32_t& i) {
            uint32_t n;

            // prefer merging with batch nodes first, pushing them to the free list
            if (i < batch_count) {
                n = batch[i];
                if (nodes[run].end() == nodes[n].offset) {
                    nodes[run].size += nodes[n].size;
                    nodes[n].right = free;
                    free = n;
                    ++i;
                    return true;
                }
            }

            // fall back to treap nodes if possible, removing from heap + treap and
            // pushing to free list
            n = treap_next(root, nodes[run].end());
            if (n != NIL && nodes[run].end() == nodes[n].offset) {
                nodes[run].size += nodes[n].size;
                heap_erase(n);
                root = treap_erase(root, n);
                nodes[n].right = free;
                free = n;
                return true;
            }

            // run is complete
            return false;
        }

        /* Purge the `batch` array and coalesce adjacent free blocks, informing the
        operating system that the physical memory backing them can be reclaimed. */
        constexpr void coalesce() {
            radix_sort();

            // process each run in the sorted batch list, assuming there is at least one
            uint32_t i = 0;
            do {
                uint32_t run = batch[i++];
                bool insert = false;

                // for the first node in each run, check for an adjacent predecessor
                // that's already in the treap or remember to insert if not
                uint32_t n = treap_prev(root, nodes[run].offset);
                if (n != NIL && nodes[n].end() == nodes[run].offset) {
                    nodes[n].size += nodes[run].size;
                    nodes[run].right = free;
                    free = run;
                    run = n;
                } else {
                    insert = true;
                }

                /// TODO: windows commit chunk updates would go here

                // cascade to the right, merging with adjacent nodes from the batch
                // list or treap until no more adjacent nodes are found
                while (cascade(run, i));

                // decommit the merged block
                if (!decommit_address_space(
                    static_cast<std::byte*>(ptr) + (nodes[run].offset * PAGE_SIZE),
                    nodes[run].size * PAGE_SIZE
                )) {
                    throw MemoryError(std::format(
                        "failed to decommit {:.3e} MiB from local address space "
                        "starting at address {:#x} and ending at address {:#x} -> {}",
                        double(nodes[run].size * PAGE_SIZE) / double(1_MiB),
                        reinterpret_cast<uintptr_t>(ptr) + (nodes[run].offset * PAGE_SIZE),
                        reinterpret_cast<uintptr_t>(ptr) +
                            ((nodes[run].offset + nodes[run].size) * PAGE_SIZE),
                        system_err_msg()
                    ));
                };

                // reinsert the merged block into the treap and/or heap if needed
                if (insert) {
                    root = treap_insert(root, run);
                    heap_insert(run);
                } else {
                    heap_fix(run);
                }
            } while (i < batch_count);

            // reset batch sizes, but don't bother zeroing out the arrays
            batch_count = 0;
            batched = 0;
        }

        /* Append a node id to the batch list.  If the batch list exceeds a memory
        threshold, then purge the batch list and coalesce adjacent free blocks.  Note
        that this method assumes the node has already been removed from both the heap
        and treap. */
        constexpr void put_node(uint32_t id) noexcept {
            batch[batch_count++] = id;
            batched += nodes[id].size;
            if (batched >= COALESCE_BUFFER) {
                coalesce();
            }
        }

        /* Request `size` pages of memory from the address space, returning a void
        pointer to the allocated region.  Note that `size` is always aligned to the
        nearest page, and the same value must always be passed to the `deallocate()`
        method along with the result of this function in order to free the allocated
        memory.  This will be called in the constructor for `arena<T, N>`. */
        [[nodiscard]] void* allocate(uint32_t pages) {
            acquire();  // ensure mapping/partition ready

            // check if there is enough space available
            uint32_t largest = heap(0);
            if (pages > nodes[largest].size) {
                if (batch_count > 0) {
                    coalesce();
                    return allocate(pages);  // try again
                }
                throw MemoryError(std::format(
                    "failed to allocate {:.3e} MiB from local address space ({:.3e} "
                    "MiB / {:.3e} MiB - {}%) -> insufficient space available",
                    double(pages * PAGE_SIZE) / double(1_MiB),
                    utilization_in_mib(),
                    size_in_mib(),
                    percent_utilization()
                ));
            }

            // remove from heap and bisect
            heap_erase(largest);
            uint32_t remaining = nodes[largest].size - pages;
            uint32_t left_size = remaining / 2;
            uint32_t right_size = remaining - left_size;
            uint32_t offset = nodes[largest].offset + left_size;

            /// TODO: windows commit chunking would go here

            // if there is one remaining half, reuse the existing node without
            // allocating a new one; otherwise, get a new node or push the current node
            // onto the free list
            if (left_size) {
                // no need to fix treap, since the left half never moved
                nodes[largest].size = left_size;
                heap_insert(largest);
                if (right_size) {
                    uint32_t child = get_node();
                    node& c = nodes[child];
                    c.offset = offset + pages;
                    c.size = right_size;
                    c.left = NIL;
                    c.right = NIL;
                    c.heap = NIL;
                    root = treap_insert(root, child);
                    heap_insert(child);
                }
            } else if (right_size) {
                root = treap_erase(root, largest);
                nodes[largest].offset = offset + pages;
                nodes[largest].size = right_size;
                root = treap_insert(root, largest);
                heap_insert(largest);
            } else {
                root = treap_erase(root, largest);
                nodes[largest].right = free;
                free = largest;
            }

            occupied += pages;
            return static_cast<std::byte*>(ptr) + (offset * PAGE_SIZE);
        }

        /* Return `size` pages of memory back to the address space.  Note that both
        `p` and `size` must match a previous call to `allocate()`, which allows this
        method to avoid validating the arguments.  This will be called in the
        destructor for `arena<T, N>`. */
        void deallocate(void* p, uint32_t pages) {
            uint32_t offset =
                (static_cast<std::byte*>(p) - static_cast<std::byte*>(ptr)) / PAGE_SIZE;

            // get a quarantined node and add it to the batch list
            uint32_t id = get_node();
            nodes[id].offset = offset;
            nodes[id].size = pages;
            nodes[id].left = NIL;
            nodes[id].right = NIL;
            nodes[id].heap = NIL;
            occupied -= pages;
            put_node(id);  // may trigger coalesce()

            release();  // attempt to unmap if in teardown state
        }

        /// TODO: these methods would allow for resizing existing allocations

        /// TODO: grow(void* p, uint32_t pages)
        /// TODO: shrink(void* p, uint32_t pages)

    public:
        [[nodiscard]] address_space() noexcept {};
        address_space(const address_space&) = delete;
        address_space(address_space&&) = delete;
        address_space& operator=(const address_space&) = delete;
        address_space& operator=(address_space&&) = delete;
        ~address_space() noexcept (false) {
            teardown = true;
            release();
        }

        /* The size of the local address space reflects the current amount of virtual
        memory that has been allocated from it in bytes (which is not necessarily equal
        to the amount of physical ram being consumed). */
        [[nodiscard]] constexpr size_t size() const noexcept { return occupied * PAGE_SIZE; }
        [[nodiscard]] constexpr ssize_t ssize() const noexcept { return ssize_t(size()); }
        [[nodiscard]] constexpr bool empty() const noexcept { return occupied == 0; }

        /// TODO: some more diagnostic methods here?

    } local_address_space {};

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
            impl::local_address_space.acquire();  // initialize thread-local arena
            ptr = reinterpret_cast<type*>(
                /// TODO: align to nearest page boundary?
                impl::local_address_space.allocate(len * sizeof(type))
            );
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
                    impl::local_address_space.deallocate(
                        reinterpret_cast<std::byte*>(ptr),
                        len * sizeof(type)
                    );
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
            } else {
                impl::local_address_space.deallocate(
                    reinterpret_cast<std::byte*>(ptr),
                    len * sizeof(type)
                );
                impl::local_address_space.release();
            }
            ptr = nullptr;
            len = 0;
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
//                         "at address {:#x} and ending at address {:#x} ({:.3e} MiB) - {}",
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
//                         "({:.3e} MiB) - {}",
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
//                     "at address {:#x} and ending at address {:#x} ({:.3e} MiB) - {}",
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
//                     "({:.3e} MiB) - {}",
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
//                     "at address {:#x} and ending at address {:#x} ({:.3e} MiB)",
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
//                 "address {:#x} and ending at address {:#x} ({:.3e} MiB) - {}",
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
//                     "at address {:#x} and ending at address {:#x} ({:.3e} MiB)",
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
//                 "address {:#x} and ending at address {:#x} ({:.3e} MiB) - {}",
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
//                     "address {:#x} ({:.3e} MiB)",
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
//                     "address {:#x} ({:.3e} MiB)",
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
//                 "address {:#x} and ending at address {:#x} ({:.3e} MiB)",
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
//                     "at address {:#x} and ending at address {:#x} ({:.3e} MiB)",
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
//                     "at address {:#x} and ending at address {:#x} ({:.3e} MiB)",
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
//                     "address {:#x} ({:.3e} MiB)",
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
//                     "address {:#x} ({:.3e} MiB)",
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
