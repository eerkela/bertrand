#ifndef BERTRAND_ALLOCATE_H
#define BERTRAND_ALLOCATE_H

#include <random>
#include <mutex>

#include "bertrand/common.h"


#ifdef _WIN32
    #include <windows.h>
    #include <errhandlingapi.h>
#elifdef __unix__
    #include <sys/mman.h>
    #include <unistd.h>
#endif


namespace bertrand {


namespace impl {
    struct address_space_tag {};

    namespace bytes {
        enum : size_t {
            B = 1,
            KiB = 1024,
            MiB = 1024 * KiB,
            GiB = 1024 * MiB,
            TiB = 1024 * GiB,
            PiB = 1024 * TiB,
            EiB = 1024 * PiB,
        };
    }

    template <typename T>
    constexpr size_t DEFAULT_ADDRESS_CAPACITY = (bytes::MiB * 8 + sizeof(T) - 1) / sizeof(T);
    template <meta::is_void T>
    constexpr size_t DEFAULT_ADDRESS_CAPACITY<T> = bytes::MiB * 8;

    /* Reserve a range of virtual addresses with the given size.  Returns a pointer to
    the start of the address range or nullptr on error. */
    [[nodiscard]] inline std::byte* map_address_space(size_t size) noexcept {
        #ifdef _WIN32
            return reinterpret_cast<std::byte*>(VirtualAlloc(
                nullptr,
                size,
                MEM_RESERVE,
                PAGE_NOACCESS
            ));
        #elifdef __unix__
            #ifdef MAP_NORESERVE
                constexpr int flags =
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE | MAP_FIXED;
            #else
                constexpr int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED;
            #endif
            void* ptr = mmap(
                nullptr,
                size,
                PROT_NONE,  // no read/write access - no pages are allocated
                flags,
                -1,  // no file descriptor
                0  // no offset
            );
            return ptr == MAP_FAILED ? nullptr : reinterpret_cast<std::byte*>(ptr);
        #else
            return nullptr;  // not supported
        #endif
    }

    /* Release a range of virtual addresses starting at the given pointer with the
    specified size, returning them to the OS.  This is the inverse of
    `map_address_space()`.  Returns `false` to indicate an error. */
    [[nodiscard]] inline bool unmap_address_space(std::byte* ptr, size_t size) noexcept {
        #ifdef _WIN32
            return VirtualFree(reinterpret_cast<void*>(ptr), 0, MEM_RELEASE) != 0;
        #elifdef __unix__
            return munmap(reinterpret_cast<void*>(ptr), size) == 0;
        #else
            return false;  // not supported
        #endif
    }

    /* Allow read/write access to a portion of a mapped address space starting at
    `ptr` and continuing for `size` bytes.  Pages will be mapped into the committed
    region by the OS when they are first accessed.  Returns the same pointer as the
    input if successful, or null if an unspecified OS error occurred during the
    permission change. */
    [[nodiscard, gnu::malloc]] inline std::byte* commit_address_space(
        std::byte* ptr,
        size_t size
    ) noexcept {
        #ifdef _WIN32
            return reinterpret_cast<std::byte*>(VirtualAlloc(
                reinterpret_cast<LPVOID>(ptr),
                size,
                MEM_COMMIT,
                PAGE_READWRITE
            ));
        #elifdef __unix__
            return mprotect(
                reinterpret_cast<void*>(ptr),
                size,
                PROT_READ | PROT_WRITE
            ) ? nullptr : ptr;
        #else
            return nullptr;  // not supported
        #endif
    }

    /* Revoke read/write access to a portion of a mapped address space starting at
    `ptr` and continuing for `size` bytes.  Committed pages within that range will be
    marked for deallocation, but the memory will not be immediately freed by the OS
    until it comes under memory pressure.  If the region is re-committed before that
    time, then the pages will still be present and will not need to be reallocated.
    Returns `false` if an error occurred during the commitment. */
    [[nodiscard]] inline bool decommit_address_space(
        std::byte* ptr,
        size_t size
    ) noexcept {
        #ifdef _WIN32
            return VirtualFree(
                reinterpret_cast<LPVOID>(ptr),
                length,
                MEM_DECOMMIT
            ) == 0;
        #elifdef __unix__
            return mprotect(
                reinterpret_cast<void*>(ptr),
                size,
                PROT_NONE
            ) != 0;
        #else
            return false;  // not supported
        #endif
    }

    /* Revoke read/write access and eagerly reclaim all physical memory associated
    with a portion of a mapped address space starting at `ptr` and continuing for
    `size` bytes.  This is a stronger version of `decommit_address_space()` which
    encourages the OS to reclaim pages more aggressively, assuming that they are
    unlikely to be needed in the future.  Like `decommit_address_space()`, this
    function will return `false` if an error occurred during the operation. */
    [[nodiscard]] inline bool purge_address_space(std::byte* ptr, size_t size) noexcept {
        if (decommit_address_space(ptr, size)) {
            #ifdef _WIN32
                return true;  // nothing to do
            #elifdef __unix__
                return madvise(
                    reinterpret_cast<void*>(ptr),
                    size,
                    MADV_DONTNEED
                ) == 0;
            #endif
        }
        return false;  // decommit failed or not supported
    }

    /* In the event of a syscall error, get an explanatory error message in a
    thread-safe way. */
    [[nodiscard]] inline std::string system_err_msg() {
        #ifdef _WIN32
            return
                std::string("windows: ") +
                std::system_category().message(GetLastError());  // thread-safe
        #elifdef __unix__
            char buffer[1024];
            return
                std::string("unix: ") +
                strerror_r(errno, buffer, 1024);  // thread-safe
        #else
            return "Unknown OS error";
        #endif
    }

}


#ifdef BERTRAND_PAGE_SIZE
    constexpr size_t PAGE_SIZE = BERTRAND_PAGE_SIZE;
#else
    constexpr size_t PAGE_SIZE = impl::bytes::KiB * 4;
#endif


#ifdef BERTRAND_ARENA_SIZE
    constexpr size_t ARENA_SIZE = BERTRAND_ARENA_SIZE;
#else
    constexpr size_t ARENA_SIZE = impl::bytes::GiB * 128;
#endif


#ifdef BERTRAND_NUM_ARENAS
    constexpr size_t NUM_ARENAS = BERTRAND_NUM_ARENAS;
#else
    constexpr size_t NUM_ARENAS = 8;
#endif


static_assert(
    PAGE_SIZE == 0 || ARENA_SIZE % PAGE_SIZE == 0,
    "arena size must be a multiple of the page size"
);
static_assert(
    ARENA_SIZE == 0 || ARENA_SIZE >= (PAGE_SIZE * 2),
    "arena must contain at least two pages of memory to store a freelist and "
    "public partition"
);


/* True if the current system supports virtual address reservation.  False
otherwise. */
constexpr bool HAS_ARENA = ARENA_SIZE > 0 && NUM_ARENAS > 0 && PAGE_SIZE > 0;


namespace meta {

    template <typename Alloc>
    concept address_space = inherits<Alloc, impl::address_space_tag>;

    template <typename Alloc, typename T>
    concept address_space_for =
        address_space<Alloc> &&
        std::same_as<typename unqualify<Alloc>::type, T>;

    template <typename Alloc, typename T>
    concept allocator_or_space_for =
        allocator_for<Alloc, T> ||
        address_space_for<Alloc, T>;

    template <typename T, size_t N>
    concept address_space_args = HAS_ARENA && meta::unqualified<T>;

    template <typename T, size_t N>
    concept small_address_space_args =
        address_space_args<T, N> &&
        N * sizeof(std::conditional_t<meta::is_void<T>, std::byte, T>) < PAGE_SIZE;

}


template <typename T = void, size_t N = impl::DEFAULT_ADDRESS_CAPACITY<T>>
    requires (meta::address_space_args<T, N>)
struct address_space;


namespace impl {

    inline bool valid_page_size = [] {
        #ifdef _WIN32
            SYSTEM_INFO si;
            GetSystemInfo(&si);
            if (si.dwPageSize != PAGE_SIZE) {
                std::cerr << std::format(
                    "warning: `BERTRAND_PAGE_SIZE` ({} bytes) does not match "
                    "system page size ({} bytes).  This may cause misaligned virtual "
                    "address spaces and undefined behavior.  Please recompile with "
                    "`BERTRAND_PAGE_SIZE` set to the correct value to silence this "
                    "warning",
                    PAGE_SIZE,
                    si.dwPageSize
                ) << std::endl;
            }
        #elifdef __unix__
            if (sysconf(_SC_PAGESIZE) != PAGE_SIZE) {
                std::cerr << std::format(
                    "warning: `BERTRAND_PAGE_SIZE` ({} bytes) does not match "
                    "system page size ({} bytes).  This may cause misaligned virtual "
                    "address spaces and undefined behavior.  Please recompile with "
                    "`BERTRAND_PAGE_SIZE` set to the correct value to silence this "
                    "warning",
                    PAGE_SIZE,
                    sysconf(_SC_PAGESIZE)
                ) << std::endl;
            }
        #endif
        return true;
    }();

    /* A pool of virtual arenas backing the `address_space<T, N>` class.  These arenas
    are allocated upfront at program startup and reused in a manner similar to
    `malloc()`/`free()`, minimizing syscall overhead when apportioning spaces in
    downstream code.  Provides thread-safe access to each arena, and implements a
    simple load-balancing algorithm to ensure that the arenas are evenly utilized. */
    struct global_address_space {

        /* An individual arena, representing a contiguous virtual address space of
        length `ARENA_SIZE` with a private partition to store an allocator freelist. */
        struct arena {
        private:
            template <typename T, size_t N> requires (meta::address_space_args<T, N>)
            friend struct bertrand::address_space;
            friend global_address_space;

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
            size_t m_id;  // arena ID in range [0, NUM_ARENAS)
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
                        double(PAGE_SIZE) / double(impl::bytes::MiB),
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
                                        double(PAGE_SIZE) / double(impl::bytes::MiB),
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
            invoked immediately after `global_address_space.acquire()`. */
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
                            double(ARENA_SIZE) / double(impl::bytes::MiB)
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
            [0, NUM_ARENAS). */
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
        template <typename T, size_t N> requires (meta::address_space_args<T, N>)
        friend struct bertrand::address_space;

        struct storage;
        friend storage;

        global_address_space() = default;
        ~global_address_space() noexcept {
            if (!unmap_address_space(m_data, ARENA_SIZE * NUM_ARENAS)) {
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
            std::byte* ptr = map_address_space(ARENA_SIZE * NUM_ARENAS);
            if (!ptr) {
                throw MemoryError(std::format(
                    "Failed to map address space for {} arenas starting at "
                    "address {:#x} and ending at address {:#x} ({:.2e} MiB) - {}",
                    ARENA_SIZE * NUM_ARENAS,
                    reinterpret_cast<uintptr_t>(ptr),
                    reinterpret_cast<uintptr_t>(ptr) + ARENA_SIZE * NUM_ARENAS,
                    double(ARENA_SIZE * NUM_ARENAS) / double(impl::bytes::MiB),
                    system_err_msg()
                ));
            }
            return ptr;
        }();

        /* Arenas are stored in an array where each index is equal to the arena ID. */
        std::array<arena, NUM_ARENAS> m_arenas = []<size_t... Is>(
            std::index_sequence<Is...>,
            std::byte* data
        ) {
            return std::array<arena, NUM_ARENAS>{arena{
                Is,
                data + ARENA_SIZE * Is
            }...};
        }(std::make_index_sequence<NUM_ARENAS>{}, m_data);

        /* Search for an available arena for a requested address space of size
        `capacity`.  A null pointer will be returned if all arenas are full.
        Otherwise, the arena will be returned in a locked state, requiring the caller
        to manually unlock its mutex. */
        [[nodiscard]] arena* acquire(size_t capacity) {
            static thread_local std::mt19937 rng{std::random_device()()};
            static thread_local std::uniform_int_distribution<size_t> dist{
                0,
                NUM_ARENAS - 1
            };

            constexpr size_t max_neighbors = 4;
            constexpr size_t neighbors =
                max_neighbors < NUM_ARENAS ? max_neighbors : NUM_ARENAS;

            // concurrent load balancing is performed stochastically by generating
            // a random index into the arena array and building a neighborhood of
            // the next 4 arenas as a ring buffer sorted by utilization.
            size_t index = 0;
            if constexpr (NUM_ARENAS > max_neighbors) {
                index = dist(rng) % NUM_ARENAS;
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
                    ring{&arenas[(index + Is) % NUM_ARENAS]}...
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
                double(capacity) / double(impl::bytes::MiB)
            ));
        }

    public:
        global_address_space(const global_address_space&) = delete;
        global_address_space(global_address_space&&) = delete;
        global_address_space& operator=(const global_address_space&) = delete;
        global_address_space& operator=(global_address_space&&) = delete;

        /* Get a reference to the global address space singleton. */
        [[nodiscard]] static global_address_space& get() noexcept;

        /* The number of arenas in the global address space.  This is a compile-time
        constant that is set by the `BERTRAND_NUM_ARENAS` build flag, and defaults to 8
        if not specified.  More arenas may reduce contention in multithreaded
        environments. */
        [[nodiscard]] static constexpr size_t size() noexcept {
            return NUM_ARENAS;
        }

        /* Access a specific arena by its ID, which is a sequential integer from 0 to
        `size()`. */
        [[nodiscard]] const arena& operator[](size_t id) const {
            if (id >= NUM_ARENAS) {
                throw IndexError(std::format(
                    "Arena ID {} is out of bounds for global address space with {} "
                    "arenas.",
                    id,
                    NUM_ARENAS
                ));
            }
            return m_arenas[id];
        }

        /* Iterate over the arenas one-by-one to collect usage statistics. */
        [[nodiscard]] const arena* begin() const noexcept {
            if constexpr (NUM_ARENAS == 0) {
                return nullptr;
            } else {
                return &m_arenas[0];
            }
        }

        [[nodiscard]] const arena* end() const noexcept {
            if constexpr (NUM_ARENAS == 0) {
                return nullptr;
            } else {
                return &m_arenas[NUM_ARENAS];
            }
        }
    };

    /* The global address space singleton is stored in an uninitialized private buffer
    that gets initialized first thing before any other global or static constructors
    are run, and last thing after any destructors that may need it.  The
    `global_address_space::get()` method is the only way to access this instance, and
    another instance cannot be created, destroyed, or unsafely modified in any other
    context. */
    struct global_address_space::storage {
    private:
        friend global_address_space;

        inline static bool initialized = false;
        alignas(global_address_space) inline static unsigned char buffer[
            sizeof(global_address_space)
        ];

        [[gnu::constructor(101)]] static void init() {
            if constexpr (HAS_ARENA) {
                if (!initialized) {
                    new (buffer) global_address_space();
                    initialized = true;
                }
            }
        }

        [[gnu::destructor(65535)]] static void finalize() {
            if constexpr (HAS_ARENA) {
                if (initialized) {
                    reinterpret_cast<global_address_space*>(
                        buffer
                    )->~global_address_space();
                    initialized = false;
                }
            }
        }
    };

    [[nodiscard]] inline global_address_space& global_address_space::get() noexcept {
        static_assert(
            HAS_ARENA,
            "The current system does not support virtual address spaces.  Please set "
            "the `BERTRAND_ARENA_SIZE`, `BERTRAND_NUM_ARENAS`, and `BERTRAND_PAGE_SIZE` "
            "build flags to positive values to enable this feature, or condition this "
            "code path on the state of the constexpr `HAS_ARENA` variable to provide "
            "an alternative."
        );
        return *reinterpret_cast<global_address_space*>(storage::buffer);
    }

}


/* A contiguous region of reserved virtual addresses (pointers), into which physical
memory can be allocated.

Note that this does not allocate memory by itself.  Instead, it denotes a range of
forbidden pointer values that the operating system will not assign to any other source
within the same process.  Physical memory can then be requested to back these addresses
as needed, which will cause the operating system to allocate individual pages and map
them into the virtual address space using the CPU's Memory Management Unit (MMU).  Such
hardware is not guaranteed to be present on all systems (particularly for embedded
systems), in which case a `MemoryError` will be raised upon construction.  Otherwise,
the address space will have been reserved, and any physical pages assigned to it will
be automatically released when the address space is destroyed.

The template parameter `T` can be used to specify the type of data that will be stored
inside the address space.  The space itself is held as a `T*` pointer, and the size
used to construct it indicates how many instances of `T` can be stored within.  If
`T = void`, then the size is interpreted as a raw number of bytes, which can be used
to store arbitrary data.

The template parameter `N` indicates the size of space to reserve in units of `T`, as
described above.  The default value equates to roughly 8 MiB of contiguous address
space, subdivided into segments of type `T`.  This is usually sufficient for small to
medium-sized data structures, but can be increased arbitrarily if necessary, up to the
system's maximum virtual address (~256 TiB on most 64-bit systems).

This class is primarily meant to implement low-level memory allocation for arena
allocators and dynamic data structures, such as high-performance vectors.  Since the
virtual address space is reserved ahead of time and decoupled from physical memory, it
is possible to implement these data structures without ever needing to relocate objects
in memory, which is ordinarily a costly operation that results in fragmentation and
possible dangling pointers.  By using a virtual address space, the data structure can
be trivially resized by simply allocating additional pages of physical memory within
the same address range, without changing the address of existing objects.  This
improves both performance and safety, since there is no risk of pointer invalidation or
memory leaks due to growth of the data structure (although invalidation can still occur
if relocation occurs for other reasons, such as by removing an element from the middle
of a vector).  It also shields the user somewhat from heap corruption vulnerabilities
and out-of-bounds reads/writes, since most of the address space is protected by the
operating system, and additional hardening can be trivially applied.

Allocating and deallocating address spaces is considered to be thread-safe, but the
space itself has no built-in synchronization beyond that.  If the same space is
expected to be accessible from multiple threads, then the user must ensure that
concurrent access is properly synchronized, either by explicit locking or by
partitioning the address space into separate regions for each thread.  With a careful
implementation, this technically allows for direct read/write access between threads
through the shared address space, which enables complex messaging patterns and
low-level data sharing, although that is not trivial to implement correctly. */
template <typename T, size_t N> requires (meta::address_space_args<T, N>)
struct address_space : impl::address_space_tag {
    using arena = impl::global_address_space::arena;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using type = T;
    using value_type = std::conditional_t<meta::is_void<T>, std::byte, T>;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;

    enum : size_type {
        B = impl::bytes::B,
        KiB = impl::bytes::KiB,
        MiB = impl::bytes::MiB,
        GiB = impl::bytes::GiB,
        TiB = impl::bytes::TiB,
    };

    /* Indicates whether this address space falls into the small space optimization.
    Such spaces cannot be copied or moved without downstream, implementation-defined
    logic. */
    static constexpr bool SMALL = false;

    /* The default capacity to reserve if no template override is given.  This defaults
    to a maximum of ~8 MiB of total storage, evenly divided into contiguous segments of
    type `T`. */
    static constexpr size_type DEFAULT_CAPACITY = impl::DEFAULT_ADDRESS_CAPACITY<T>;

private:

    static constexpr size_t GUARD_SIZE =
        ((sizeof(value_type) + PAGE_SIZE - 1) / PAGE_SIZE) * PAGE_SIZE;

    static constexpr size_t TOTAL_SIZE =
        GUARD_SIZE + (N * sizeof(value_type)) + GUARD_SIZE;

public:

    /* Default constructor.  Reserves address space, but does not commit any memory. */
    [[nodiscard]] address_space() :
        m_arena(impl::global_address_space::get().acquire(TOTAL_SIZE)),
        m_data(reinterpret_cast<pointer>(m_arena->reserve(TOTAL_SIZE) + GUARD_SIZE))
    {}

    address_space(const address_space&) = delete;
    address_space& operator=(const address_space&) = delete;

    [[nodiscard]] address_space(address_space&& other) noexcept :
        m_arena(other.m_arena), m_data(other.m_data)
    {
        other.m_arena = nullptr;
        other.m_data = nullptr;
    }

    [[maybe_unused]] address_space& operator=(address_space&& other) noexcept(!DEBUG) {
        if (this != &other) {
            if (m_data) {
                std::byte* p = reinterpret_cast<std::byte*>(m_data);
                if (!impl::purge_address_space(p, nbytes())) {
                    throw MemoryError(std::format(
                        "failed to decommit pages from virtual address space starting "
                        "at address {:#x} and ending at address {:#x} ({:.2e} MiB) - {}",
                        reinterpret_cast<uintptr_t>(m_data),
                        reinterpret_cast<uintptr_t>(m_data) + nbytes(),
                        double(nbytes()) / double(impl::bytes::MiB),
                        impl::system_err_msg()
                    ));
                }
                if (!m_arena->recycle(p - GUARD_SIZE, TOTAL_SIZE)) {
                    throw MemoryError(std::format(
                        "failed to commit additional pages to freelist for arena {} "
                        "starting at address {:#x} and ending at address {:#x} "
                        "({:.2e} MiB) - {}",
                        m_arena->id(),
                        reinterpret_cast<uintptr_t>(m_data),
                        reinterpret_cast<uintptr_t>(m_data) + nbytes(),
                        double(nbytes()) / double(impl::bytes::MiB),
                        impl::system_err_msg()
                    ));
                }
            }
            m_arena = other.m_arena;
            m_data = other.m_data;
            other.m_arena = nullptr;
            other.m_data = nullptr;
        }
        return *this;
    }

    /* Unreserve the address space upon destruction, returning the virtual capacity to
    the arena's freelist and allowing the operating system to reclaim physical pages
    for future allocations. */
    ~address_space() {
        m_arena = nullptr;
        if (m_data) {
            std::byte* p = reinterpret_cast<std::byte*>(m_data);
            if (!impl::purge_address_space(p, nbytes())) {
                throw MemoryError(std::format(
                    "failed to decommit pages from virtual address space starting "
                    "at address {:#x} and ending at address {:#x} ({:.2e} MiB) - {}",
                    reinterpret_cast<uintptr_t>(m_data),
                    reinterpret_cast<uintptr_t>(m_data) + nbytes(),
                    double(nbytes()) / double(impl::bytes::MiB),
                    impl::system_err_msg()
                ));
            }
            if (!m_arena->recycle(p - GUARD_SIZE, TOTAL_SIZE)) {
                throw MemoryError(std::format(
                    "failed to commit additional pages to freelist for arena {} "
                    "starting at address {:#x} and ending at address {:#x} "
                    "({:.2e} MiB) - {}",
                    m_arena->id(),
                    reinterpret_cast<uintptr_t>(m_data),
                    reinterpret_cast<uintptr_t>(m_data) + nbytes(),
                    double(nbytes()) / double(impl::bytes::MiB),
                    impl::system_err_msg()
                ));
            }
            m_data = nullptr;
        }
    }

    /* True if the address space no longer owns virtual memory.  False otherwise.
    Currently, this only occurs after an address space has been moved from, leaving it
    in an empty state. */
    [[nodiscard]] explicit operator bool() const noexcept { return m_data; }

    /* Return the maximum size of the virtual address space in units of `T`.  If
    `T` is void, then this refers to the total size (in bytes) of the address space
    itself. */
    [[nodiscard]] static constexpr size_type size() noexcept { return N; }

    /* Return the maximum size of the address space in bytes.  If `T` is void, then
    this is equivalent to `size()`. */
    [[nodiscard]] static constexpr size_type nbytes() noexcept { return N * sizeof(value_type); }

    /* Get a pointer to the beginning of the reserved address space.  If `T` is void,
    then the pointer will be returned as `std::byte*`. */
    [[nodiscard]] pointer data() noexcept { return m_data; }
    [[nodiscard]] const_pointer data() const noexcept { return m_data; }

    /* Iterate over the reserved addresses.  If `T` is void, then each element is
    represented as a `std::byte`. */
    [[nodiscard]] iterator begin() noexcept { return m_data; }
    [[nodiscard]] const_iterator begin() const noexcept { return m_data; }
    [[nodiscard]] const_iterator cbegin() const noexcept { return m_data;}
    [[nodiscard]] iterator end() noexcept { return begin() + N; }
    [[nodiscard]] const_iterator end() const noexcept { return begin() + N; }
    [[nodiscard]] const_iterator cend() const noexcept { return begin() + N; }
    [[nodiscard]] reverse_iterator rbegin() noexcept { return {end()}; }
    [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return {end()}; }
    [[nodiscard]] const_reverse_iterator crbegin() const noexcept { return {end()}; }
    [[nodiscard]] reverse_iterator rend() noexcept { return {begin()}; }
    [[nodiscard]] const_reverse_iterator rend() const noexcept { return {begin()}; }
    [[nodiscard]] const_reverse_iterator crend() const noexcept { return {begin()}; }

    /* Manually commit physical memory to the address space.  If `T` is not void, then
    `offset` and `length` are both multiplied by `sizeof(T)` to determine the actual
    offsets for the relevant syscall.  The result is a pointer to the start of the
    committed memory, and a `MemoryError` may be thrown if committing the memory
    resulted in an OS error, with the original message being forwarded to the user.

    Individual arena implementations are expected to wrap this method to make the
    interface more convenient.  This simply abstracts the low-level OS hooks to make
    them cross-platform. */
    [[maybe_unused, gnu::malloc]] pointer allocate(size_type offset, size_type length) {
        if constexpr (DEBUG) {
            if (offset + length > N) {
                throw MemoryError(std::format(
                    "attempted to commit memory at offset {} with length {}, "
                    "which exceeds the size of the virtual address space starting "
                    "at address {:#x} and ending at address {:#x} ({:.2e} MiB)",
                    offset,
                    length,
                    reinterpret_cast<uintptr_t>(m_data),
                    reinterpret_cast<uintptr_t>(m_data) + nbytes(),
                    double(nbytes()) / double(impl::bytes::MiB)
                ));
            }
        }
        pointer result = reinterpret_cast<pointer>(impl::commit_address_space(
            m_data,
            offset * sizeof(value_type),
            length * sizeof(value_type)
        ));
        if (result == nullptr) {
            throw MemoryError(std::format(
                "failed to commit pages to virtual address space starting at "
                "address {:#x} and ending at address {:#x} ({:.2e} MiB) - {}",
                reinterpret_cast<uintptr_t>(m_data),
                reinterpret_cast<uintptr_t>(m_data) + nbytes(),
                double(nbytes()) / double(impl::bytes::MiB),
                impl::system_err_msg()
            ));
        }
        return result;
    }

    /* Manually release physical memory from the address space.  If `T` is not void,
    then `offset` and `length` are both multiplied by `sizeof(T)` to determine the
    actual offsets for the relevant syscall.  A `MemoryError` may be thrown if
    decommitting the memory resulted in an OS error, with the original message being
    forwarded to the user.

    Individual arena implementations are expected to wrap this method to make the
    interface more convenient.  This simply abstracts the low-level OS hooks to make
    them cross-platform.  Note that this does not remove addresses from the space
    itself, which is always handled automatically by the destructor. */
    void deallocate(size_type offset, size_type length) {
        if constexpr (DEBUG) {
            if (offset + length > N) {
                throw MemoryError(std::format(
                    "attempted to decommit memory at offset {} with length {}, "
                    "which exceeds the size of the virtual address space starting "
                    "at address {:#x} and ending at address {:#x} ({:.2e} MiB)",
                    offset,
                    length,
                    reinterpret_cast<uintptr_t>(m_data),
                    reinterpret_cast<uintptr_t>(m_data) + nbytes(),
                    double(nbytes()) / double(impl::bytes::MiB)
                ));
            }
        }
        if (!impl::decommit_address_space(
            m_data,
            offset * sizeof(value_type),
            length * sizeof(value_type)
        )) {
            throw MemoryError(std::format(
                "failed to decommit pages from virtual address space starting at "
                "address {:#x} and ending at address {:#x} ({:.2e} MiB) - {}",
                reinterpret_cast<uintptr_t>(m_data),
                reinterpret_cast<uintptr_t>(m_data) + nbytes(),
                double(nbytes()) / double(impl::bytes::MiB),
                impl::system_err_msg()
            ));
        };
    }

    /* Construct a value that was allocated from this address space using placement
    new.  Propagates any errors that emanate from the constructor for `T`. */
    template <typename... Args>
        requires (meta::not_void<T> && meta::constructible_from<T, Args...>)
    void construct(pointer p, Args&&... args)
        noexcept(!DEBUG && noexcept(new (p) T(std::forward<Args>(args)...)))
    {
        if constexpr (DEBUG) {
            if (p < begin() || p >= end()) {
                throw MemoryError(std::format(
                    "pointer at address {:#x} was not allocated from the virtual "
                    "address space starting at address {:#x} and ending at "
                    "address {:#x} ({:.2e} MiB)",
                    reinterpret_cast<uintptr_t>(p),
                    reinterpret_cast<uintptr_t>(m_data),
                    reinterpret_cast<uintptr_t>(m_data) + nbytes(),
                    double(nbytes()) / double(impl::bytes::MiB)
                ));
            }
        }
        new (p) T(std::forward<Args>(args)...);
    }

    /* Destroy a value that was allocated from this address space by calling its
    destructor in-place.  Note that this does not deallocate the underlying memory,
    which only occurs when the address space is destroyed, or when memory is explicitly
    decommitted using the `deallocate()` method.  Any errors emanating from the
    destructor for `T` will be propagated. */
    void destroy(pointer p) noexcept(!DEBUG && meta::nothrow::destructible<T>) {
        if constexpr (DEBUG) {
            if (p < begin() || p >= end()) {
                throw MemoryError(std::format(
                    "pointer at address {:#x} was not allocated from the virtual "
                    "address space starting at address {:#x} and ending at "
                    "address {:#x} ({:.2e} MiB)",
                    reinterpret_cast<uintptr_t>(p),
                    reinterpret_cast<uintptr_t>(m_data),
                    reinterpret_cast<uintptr_t>(m_data) + nbytes(),
                    double(nbytes()) / double(impl::bytes::MiB)
                ));
            }
        }
        if constexpr (!meta::trivially_destructible<T>) {
            p->~T();
        }
    }

private:
    arena* m_arena;
    pointer m_data;
};


/* Small space optimization for `address_space<T, N>`, where `N * sizeof(T)` is less
than one full page in memory.

This bypasses the virtual memory allocator and instead uses an inline buffer to store
the contents of the address space.  Such an optimization alleviates contention and
overall pressure on the allocator, reducing the number of syscalls and improving
performance for small, transient data structures.  It also limits overall memory usage,
since we avoid allocating partial pages for small spaces of a known size. */
template <typename T, size_t N> requires (meta::small_address_space_args<T, N>)
struct address_space<T, N> : impl::address_space_tag {
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    using type = T;
    using value_type = std::conditional_t<meta::is_void<T>, std::byte, T>;
    using reference = value_type&;
    using const_reference = const value_type&;
    using pointer = value_type*;
    using const_pointer = const value_type*;
    using iterator = pointer;
    using const_iterator = const_pointer;
    using reverse_iterator = std::reverse_iterator<iterator>;
    using const_reverse_iterator = const std::reverse_iterator<const_iterator>;

    enum : size_type {
        B = impl::bytes::B,
        KiB = impl::bytes::KiB,
        MiB = impl::bytes::MiB,
        GiB = impl::bytes::GiB,
        TiB = impl::bytes::TiB,
    };

    /* Indicates whether this address space falls into the small space optimization.
    Such spaces cannot be copied or moved without downstream, implementation-defined
    logic. */
    static constexpr bool SMALL = true;

    /* The default capacity to reserve if no template override is given.  This defaults
    to a maximum of ~8 MiB of total storage, evenly divided into contiguous segments of
    type `T`. */
    static constexpr size_type DEFAULT_CAPACITY = impl::DEFAULT_ADDRESS_CAPACITY<T>;

    /* Default constructor.  Allocates uninitialized storage equal to the size of the
    physical space, initialized to zero. */
    [[nodiscard]] address_space() noexcept = default;

    address_space(const address_space& other) = delete;

    address_space(address_space&& other) = delete;

    address_space& operator=(const address_space& other) = delete;

    address_space& operator=(address_space&& other) = delete;

    /* True if the physical space is not empty.  False otherwise. */
    [[nodiscard]] explicit operator bool() const noexcept { return N; }

    /* Return the maximum size of the physical space in units of `T`.  If `T` is void,
    then this refers to the total size (in bytes) of the physical space itself. */
    [[nodiscard]] static constexpr size_type size() noexcept { return N; }

    /* Return the maximum size of the physical space in bytes.  If `T` is void, then
    this is equivalent to `size()`. */
    [[nodiscard]] static constexpr size_type nbytes() noexcept { return N * sizeof(value_type); }

    /* Get a pointer to the beginning of the reserved physical space.  If `T` is void,
    then the pointer will be returned as `std::byte*`. */
    [[nodiscard]] pointer data() noexcept { return reinterpret_cast<pointer>(m_storage); }
    [[nodiscard]] const_pointer data() const noexcept {
        return reinterpret_cast<const_pointer>(m_storage);
    }

    /* Iterate over the reserved addresses.  If `T` is void, then each element is
    represented as a `std::byte`. */
    [[nodiscard]] iterator begin() noexcept {
        return reinterpret_cast<iterator>(m_storage);
    }
    [[nodiscard]] const_iterator begin() const noexcept {
        return reinterpret_cast<const_iterator>(m_storage);
    }
    [[nodiscard]] const_iterator cbegin() const noexcept {
        return reinterpret_cast<const_iterator>(m_storage);
    }
    [[nodiscard]] iterator end() noexcept { return begin() + N; }
    [[nodiscard]] const_iterator end() const noexcept { return begin() + N; }
    [[nodiscard]] const_iterator cend() const noexcept { return begin() + N; }
    [[nodiscard]] reverse_iterator rbegin() noexcept { return {end()}; }
    [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return {end()}; }
    [[nodiscard]] const_reverse_iterator crbegin() const noexcept { return {end()}; }
    [[nodiscard]] reverse_iterator rend() noexcept { return {begin()}; }
    [[nodiscard]] const_reverse_iterator rend() const noexcept { return {begin()}; }
    [[nodiscard]] const_reverse_iterator crend() const noexcept { return {begin()}; }

    /* Return a pointer to the beginning of a contiguous region of the physical space.
    If `T` is not void, then `offset` and `length` are both multiplied by `sizeof(T)`
    to determine the actual offsets. */
    [[maybe_unused, gnu::malloc]] pointer allocate(
        size_type offset,
        size_type length
    ) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (offset + length > N) {
                throw MemoryError(std::format(
                    "attempted to commit memory at offset {} with length {}, "
                    "which exceeds the size of the physical space starting "
                    "at address {:#x} and ending at address {:#x} ({:.2e} MiB)",
                    offset,
                    length,
                    reinterpret_cast<uintptr_t>(m_storage),
                    reinterpret_cast<uintptr_t>(m_storage) + nbytes(),
                    double(nbytes()) / double(impl::bytes::MiB)
                ));
            }
        }
        return reinterpret_cast<pointer>(m_storage + offset * sizeof(value_type));
    }

    /* Zero out the physical memory associated with the given range.  If `T` is not
    void, then `offset` and `length` are both multiplied by `sizeof(T)` to determine
    the actual offsets. */
    void deallocate(size_type offset, size_type length) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (offset + length > N) {
                throw MemoryError(std::format(
                    "attempted to decommit memory at offset {} with length {}, "
                    "which exceeds the size of the physical space starting "
                    "at address {:#x} and ending at address {:#x} ({:.2e} MiB)",
                    offset,
                    length,
                    reinterpret_cast<uintptr_t>(m_storage),
                    reinterpret_cast<uintptr_t>(m_storage) + nbytes(),
                    double(nbytes()) / double(impl::bytes::MiB)
                ));
            }
        }
        std::memset(
            m_storage + offset * sizeof(value_type),
            static_cast<unsigned char>(0),
            length * sizeof(value_type)
        );
    }

    /* Construct a value that was allocated from this physical space using placement
    new.  Propagates any errors that emanate from the constructor for `T`. */
    template <typename... Args>
        requires (meta::not_void<T> && meta::constructible_from<T, Args...>)
    void construct(pointer p, Args&&... args)
        noexcept(!DEBUG && noexcept(new (p) T(std::forward<Args>(args)...)))
    {
        if constexpr (DEBUG) {
            if (p < begin() || p >= end()) {
                throw MemoryError(std::format(
                    "pointer at address {:#x} was not allocated from the physical "
                    "space starting at address {:#x} and ending at "
                    "address {:#x} ({:.2e} MiB)",
                    reinterpret_cast<uintptr_t>(p),
                    reinterpret_cast<uintptr_t>(m_storage),
                    reinterpret_cast<uintptr_t>(m_storage) + nbytes(),
                    double(nbytes()) / double(impl::bytes::MiB)
                ));
            }
        }
        new (p) T(std::forward<Args>(args)...);
    }

    /* Destroy a value that was allocated from this physical space by calling its
    destructor in-place.  Note that this does not deallocate the memory itself, which
    only occurs when the physical space is destroyed.  Any errors emanating from the
    destructor for `T` will be propagated. */
    void destroy(pointer p) noexcept(!DEBUG && meta::nothrow::destructible<T>) {
        if constexpr (DEBUG) {
            if (p < begin() || p >= end()) {
                throw MemoryError(std::format(
                    "pointer at address {:#x} was not allocated from the physical "
                    "space starting at address {:#x} and ending at "
                    "address {:#x} ({:.2e} MiB)",
                    reinterpret_cast<uintptr_t>(p),
                    reinterpret_cast<uintptr_t>(m_storage),
                    reinterpret_cast<uintptr_t>(m_storage) + nbytes(),
                    double(nbytes()) / double(impl::bytes::MiB)
                ));
            }
        }
        if constexpr (!meta::trivially_destructible<T>) {
            p->~T();
        }
    }

private:
    alignas(value_type) unsigned char m_storage[N * sizeof(value_type)];
};


}  // namespace bertrand


#endif  // BERTRAND_ALLOCATE_H
