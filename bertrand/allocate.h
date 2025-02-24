#ifndef BERTRAND_ALLOCATE_H
#define BERTRAND_ALLOCATE_H

#include <cstdint>
#include <shared_mutex>
#include <random>


#include "bertrand/common.h"
#include "bertrand/except.h"


#ifdef _WIN32
    #include <windows.h>
#elifdef __APPLE__
    #include <sys/mman.h>
    #include <unistd.h>
#elifdef __unix__
    #include <sys/mman.h>
    #include <unistd.h>
#endif


namespace bertrand {


namespace impl {

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
    time, then the pages will still be present and will not need to be reallocated. */
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
    unlikely to be needed in the future. */
    [[nodiscard]] inline bool purge_address_space(std::byte* ptr, size_t size) noexcept {
        if (!decommit_address_space(ptr, size)) {
            return false;  // decommit failed
        }
        #ifdef _WIN32
            return true;  // nothing to do
        #elifdef __unix__
            return madvise(
                reinterpret_cast<void*>(ptr),
                size,
                MADV_DONTNEED
            ) == 0;
        #else
            return false;  // not supported
        #endif
    }

}


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


#ifdef BERTRAND_PAGE_SIZE
    constexpr size_t PAGE_SIZE = BERTRAND_PAGE_SIZE;
#else
    constexpr size_t PAGE_SIZE = impl::bytes::KiB * 4;
#endif


/* The real page size for the current system in bytes.  If the page size could not be
determined, then this evaluates to zero. */
inline size_t ACTUAL_PAGE_SIZE = [] {
    #ifdef _WIN32
        SYSTEM_INFO sysinfo;
        GetSystemInfo(&sysinfo);
        return static_cast<size_t>(sysinfo.dwPageSize);
    #elifdef __unix__
        return sysconf(_SC_PAGESIZE);
    #else
        return 0;
    #endif
}();


/* True if the current system supports virtual address reservation.  False
otherwise. */
inline bool HAS_VIRTUAL_ADDRESS = [] {
    if (ACTUAL_PAGE_SIZE == 0) {
        return false;
    }
    std::byte* ptr = impl::map_address_space(ACTUAL_PAGE_SIZE);
    return ptr ? impl::unmap_address_space(ptr, ACTUAL_PAGE_SIZE) : false;
}();


template <meta::unqualified T, size_t N = impl::DEFAULT_ADDRESS_CAPACITY<T>>
struct address_space;


namespace impl {
    struct address_space_tag {};
    struct physical_space_tag {};

    /* A pool of virtual arenas backing the `address_space<T, N>` class.  These arenas
    are allocated upfront at program startup and reused in a manner similar to
    `malloc()`/`free()`, minimizing syscall overhead when apportioning spaces in
    downstream code.  Provides thread-safe access to each arena, and implements a
    simple load-balancing algorithm to ensure that the arenas are evenly utilized. */
    inline struct global_address_space {
    private:
        inline static thread_local std::mt19937 rng{std::random_device()()};
        inline static thread_local std::uniform_int_distribution<size_t> dist{
            0,
            NUM_ARENAS - 1
        };

        template <meta::unqualified T, size_t N>
        friend struct address_space;

        /* An individual arena, representing a contiguous virtual address space of
        length `ARENA_SIZE` with a private partition to store an allocator freelist. */
        struct arena {
        private:
            template <meta::unqualified T, size_t N>
            friend struct address_space;
            friend global_address_space;

            struct freelist {
                static constexpr size_t PARTITION = impl::bytes::MiB * 256;

                struct node {
                    std::byte* ptr;  // start of unoccupied memory region
                    size_t length;  // length of unoccupied memory region
                    node* next = nullptr;  // next node in the freelist
                };

                node* data;  // beginning of private partition
                node* head = nullptr;  // proper head of freelist
                node* highest = nullptr;  // node with largest address
                size_t size = 0;  // number of nodes within the freelist
                size_t capacity = 0;  // number of bytes reserved for the freelist
                node* reuse = nullptr;  // tracks gaps in the node store for reuse
                mutable std::mutex mutex;  // mutex for thread-safe access

                /* Insert a node into the freelist representing an unoccupied memory
                region starting at `ptr` and continuing for `length` bytes.  This
                will attempt to merge with the previous and next nodes if possible.
                Returns the node that was inserted or merged into, or nullptr if
                an error occurred.  The mutex must be locked for the duration of this
                method. */
                [[nodiscard]] node* push(std::byte* ptr, size_t length) noexcept {
                    // find the insertion point for the new node
                    node* prev = nullptr;
                    node* next = head;
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
                            next->next = reuse;
                            reuse = next;
                            --size;
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
                    if (reuse) {
                        node* curr = reuse;
                        reuse = reuse->next;
                        curr->ptr = ptr;
                        curr->length = length;
                        curr->next = next;
                        if (prev) {
                            prev->next = curr;
                        } else {
                            head = curr;
                        }
                        ++size;
                        if (curr > highest || !highest) {
                            highest = curr;
                        }
                        return curr;
                    }

                    // otherwise, all nodes are consolidated, and we may need to
                    // allocate additional pages
                    if (size == capacity / sizeof(node)) {
                        if (!commit_address_space(
                            reinterpret_cast<std::byte*>(data) + capacity,
                            ACTUAL_PAGE_SIZE
                        )) {
                            return nullptr;
                        }
                        capacity += ACTUAL_PAGE_SIZE;
                    }

                    // allocate from the end of the contiguous region
                    node* curr = data + size;
                    curr->ptr = ptr;
                    curr->length = length;
                    curr->next = next;
                    if (prev) {
                        prev->next = curr;
                    } else {
                        head = curr;
                    }
                    ++size;
                    highest = curr;  // guaranteed to be highest
                    return curr;
                }

                /* Scan the freelist to identify an unoccupied region that can store
                at least `length` bytes, and then adjust the neighboring nodes to
                consider that region occupied.  Returns a pointer to the start of the
                unoccupied region, or nullptr if no such region exists.  The mutex
                must be locked for the duration of this method. */
                [[nodiscard]] std::byte* pop(size_t length) noexcept {
                    // find the first node large enough to store the requested length
                    node* prev = nullptr;
                    node* curr = head;
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
                                    head = curr->next;
                                }
                                curr->ptr = nullptr;
                                curr->length = 0;
                                curr->next = reuse;
                                reuse = curr;
                                --size;

                                // if the node was the highest-addressed, then we need
                                // to find the next highest node.  This can be done
                                // quickly by iterating over all addresses to the left
                                // of the node, which are guaranteed to be part of
                                // either the freelist or the reuse list.  We skip over
                                // members of the reuse list, potentially allowing them
                                // to be freed immediately after.
                                if (curr == highest) {
                                    while (highest > data && highest->ptr == nullptr) {
                                        --highest;
                                    }

                                    // if an error occurs while shrinking the list,
                                    // then we need to restore the freelist to its
                                    // original state
                                    if (!shrink()) {
                                        ++size;
                                        curr->ptr = ptr;
                                        curr->length = length;
                                        reuse = curr->next;
                                        if (prev) {
                                            curr->next = prev->next;
                                            prev->next = curr;
                                        } else {
                                            curr->next = head;
                                            head = curr;
                                        }
                                        return nullptr;
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

                bool shrink() noexcept {
                    // if the highest-addressed node is less than 3/4 the allocated
                    // capacity, then we can decommit pages to free up physical memory
                    size_t span = (highest - data) * sizeof(node);
                    if (span < (capacity - capacity / 4)) {
                        size_t new_capacity = span + ACTUAL_PAGE_SIZE - 1;
                        new_capacity /= ACTUAL_PAGE_SIZE;
                        new_capacity *= ACTUAL_PAGE_SIZE;

                        // purge the reuse list of any nodes above the new capacity
                        node* prev = nullptr;
                        node* curr = reuse;
                        node* limit = data + (new_capacity / sizeof(node));
                        node* removed = nullptr;
                        size_t n_removed = 0;
                        while (curr) {
                            if (curr >= limit) {
                                if (prev) {
                                    prev->next = curr->next;
                                } else {
                                    reuse = curr->next;
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
                            reinterpret_cast<std::byte*>(data) + new_capacity,
                            capacity - new_capacity
                        )) {
                            prev = nullptr;
                            curr = reuse;
                            while (curr) {
                                prev = curr;
                                curr = curr->next;
                            }
                            if (prev) {
                                prev->next = removed;
                            } else {
                                reuse = removed;
                            }
                            return false;
                        }
                        size -= n_removed;
                        capacity = new_capacity;
                    }
                    return true;
                }

            };

            size_t m_id;  // arena ID in range [0, NUM_ARENAS)
            size_t m_used = 0;  // number of bytes used in this arena's public partition
            std::byte* m_data = nullptr;  // pointer to the start of the public partition
            freelist m_freelist;

            arena(size_t id, std::byte* data) :
                m_id(id),
                m_freelist(reinterpret_cast<typename freelist::node*>(data))
            {
                std::unique_lock lock(m_freelist.mutex);
                if (data) {
                    m_data = data + freelist::PARTITION;
                    if (!m_freelist.push(m_data, capacity())) {
                        throw MemoryError(std::format(
                            "Failed to initialize freelist for arena {} at address "
                            "{:#x} ({:.2e} MiB).",
                            m_id,
                            reinterpret_cast<uintptr_t>(m_data),
                            double(ACTUAL_PAGE_SIZE) / double(impl::bytes::MiB)
                        ));
                    }
                }
            }

            /* Search for an unoccupied region capable of storing at least `length`
            bytes, and return a pointer to the start of that region.  Adjusts the
            freelist to consider the region occupied, and returns a null pointer if
            no such region exists.  The mutex must be locked when this method is
            called, and will be unlocked when it returns.  Typically, this method is
            invoked immediately after `global_address_space.acquire()`. */
            [[nodiscard]] std::byte* reserve(size_t length) noexcept {
                std::byte* ptr = m_freelist.pop(length);
                if (ptr) {
                    m_used += length;
                }
                m_freelist.mutex.unlock();
                return ptr;
            }

            /* Given a pointer to an unoccupied region starting at `ptr` consisting of
            `length` bytes, register the region with the arena's free list so that it
            can be used for future allocations.  Returns false if the freelist could
            not be updated. */
            [[nodiscard]] bool recycle(std::byte* ptr, size_t length) noexcept {
                m_freelist.mutex.lock();
                typename freelist::node* node = m_freelist.push(ptr, length);
                if (node) {
                    m_used -= length;
                }
                m_freelist.mutex.unlock();
                return node;
            }

        public:
            /* The ID of this arena, which is always an integer in the range
            [0, NUM_ARENAS). */
            [[nodiscard]] size_t id() const noexcept {
                return m_id;
            }

            /* The number of bytes that have been used from this arena.  This is
            always less than or equal to `capacity()`. */
            [[nodiscard]] size_t size() const noexcept {
                return m_used;
            }

            /* The total amount of virtual memory held by this arena.  This is a
            compile-time constant set by the `BERTRAND_ARENA_SIZE` build flag minus a
            constant amount reserved for the arena's private partition.  More capacity
            allows for a greater number and size of child address spaces, but can
            compete with the rest of the program's virtual address space if set too
            high.  Note that this does not refer to actual physical memory, just a
            range of pointers that are reserved for each arena. */
            [[nodiscard]] static constexpr size_t capacity() noexcept {
                return ARENA_SIZE - freelist::PARTITION;
            }

            /* A read-only pointer to the beginning of this arena's address range,
            returned as a `std::byte*`.  Pointer arithmetic can be used to navigate to
            specific sections of the address space for observation. */
            [[nodiscard]] const std::byte* data() const noexcept {
                return m_data;
            }

            /* Manually lock the arena's mutex, blocking until it is available and
            returning an RAII lock guard that automatically unlocks it upon
            destruction.  This can potentially deadlock if the mutex is already locked
            by this thread.  As long as the arena is locked, it cannot be used for
            allocation. */
            [[nodiscard]] std::unique_lock<std::mutex> lock() noexcept {
                return std::unique_lock(m_freelist.mutex);
            }

            /* Attempt to lock the arena's mutex without blocking.  If the lock was
            acquired, returns an RAII lock guard that automatically unlocks it upon
            destruction.  Otherwise, returns an empty guard that can later be used for
            deferred locking.  As long as the arena is locked, it cannot be used for
            allocation. */
            [[nodiscard]] std::unique_lock<std::mutex> try_lock() noexcept {
                return std::unique_lock(m_freelist.mutex, std::try_to_lock);
            }
        };

        /* Search for an available arena for a requested address space of size
        `capacity`.  A null pointer will be returned if all arenas are full.
        Otherwise, the arena will be returned in a locked state, requiring the caller
        to manually unlock its mutex. */
        [[nodiscard]] arena* acquire(size_t capacity) noexcept {
            if constexpr (NUM_ARENAS) {
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
                        } else if (head->ptr->m_freelist.mutex.try_lock()) {
                            return head->ptr;
                        }
                    }
                    head = head->next;
                }
            }
            return nullptr;
        }

        /// TODO: maybe this init code needs to be placed in the global constructor
        /// function?  That would probably require a default constructor for arena
        /// that does nothing, but the ordering is rather finicky.

        /* All arenas are mapped to a single contiguous address range to minimize
        syscall overhead and maximize performance for downstream `mprotect()` calls. */
        std::byte* m_data = map_address_space(ARENA_SIZE * NUM_ARENAS);

        /* Arenas are stored in an array where each index is equal to the arena ID. */
        std::array<arena, NUM_ARENAS> m_arenas = []<size_t... Is>(
            std::index_sequence<Is...>,
            std::byte* data
        ) -> std::array<arena, NUM_ARENAS> {
            if (data) {
                return {arena{Is, data + ARENA_SIZE * Is}...};
            } else {
                return {arena{Is, nullptr}...};
            }
        }(std::make_index_sequence<NUM_ARENAS>{}, m_data);

    public:

        /// TODO: destructor should be called in the global destructor function,
        /// not in the destructor of this class (or maybe also)

        ~global_address_space() noexcept {
            if (!unmap_address_space(m_data, ARENA_SIZE * NUM_ARENAS)) {
                /// NOTE: destructor is only called at program termination, so
                /// errors here are not critical.  The OS will reclaim memory
                /// anyways unless something is really wrong.
            }
        }

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

    } global_address_space;

    /* Initialize the global address space before any other global constructors are
    run. */
    [[gnu::constructor(101)]] static void initialize_global_address_space() noexcept {
        /// TODO: implement this
    }

    /* Clean up the global address space after any other global destructors are
    run. */
    [[gnu::destructor(65535)]] static void finalize_global_address_space() noexcept {
        /// TODO: implement this
    }

}


namespace meta {

    template <typename Alloc>
    concept address_space = inherits<Alloc, impl::address_space_tag>;

    template <typename Alloc, typename T>
    concept address_space_for =
        address_space<Alloc> &&
        std::same_as<typename std::remove_cvref_t<Alloc>::type, T>;

    template <typename Alloc>
    concept physical_space = inherits<Alloc, impl::physical_space_tag>;

    template <typename Alloc, typename T>
    concept physical_space_for =
        physical_space<Alloc> &&
        std::same_as<typename std::remove_cvref_t<Alloc>::type, T>;

    template <typename Alloc>
    concept space = address_space<Alloc> || physical_space<Alloc>;

    template <typename Alloc, typename T>
    concept space_for = address_space_for<Alloc, T> || physical_space_for<Alloc, T>;

    template <typename Alloc, typename T>
    concept allocator_or_space_for =
        allocator_for<Alloc, T> ||
        space_for<Alloc, T>;

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
described above.  The default value equates to roughly 32 MiB of contiguous address
space, subdivided into segments of type `T`.  This is usually sufficient for small to
medium-sized data structures, but can be increased arbitrarily if necessary, up to the
system's maximum virtual address (~256 TiB on most 64-bit systems).  If `N` is set to
zero, then the size may instead be given as a runtime parameter to the constructor
in order to model dynamic address spaces.

This class is primarily meant to implement low-level memory allocation for arena
allocators and dynamic data structures, such as high-performance vectors.  Since the
virtual address space is reserved ahead of time and decoupled from physical memory, it
is possible to implement these data structures without ever needing to relocate objects
in memory, which is ordinarily a costly operation that results in memory fragmentation
and possible dangling pointers.  By using a virtual address space, the data structure
can be trivially resized by simply allocating additional pages of physical memory
within the same address space, without changing the address of existing objects.  This
improves both performance and safety, since there is no risk of pointer invalidation or
memory leaks due to growth of the data structure (although invalidation can still occur
if relocation occurs for other reasons, such as by removing an element from the middle
of a vector).

Address spaces are not thread-safe by default, and must be protected by a lock if
multiple threads are expected to allocate data concurrently. */
template <meta::unqualified T, size_t N = impl::DEFAULT_ADDRESS_CAPACITY<T>>
struct address_space : impl::address_space_tag {
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
    };

    /* The default capacity to reserve if no template override is given.  This defaults
    to ~32 MiB of total storage, evenly divided into contiguous segments of type T. */
    static constexpr size_type DEFAULT_CAPACITY = impl::DEFAULT_ADDRESS_CAPACITY<T>;

    /* False if the address space's total capacity can only be known at runtime.
    Otherwise, equal to `N`, which is the maximum capacity of the address space in
    units of `T`.  If `T` is void, then this refers to the total memory (in bytes) that
    are reserved for the address space. */
    static constexpr size_type STATIC = N;

    /* Default constructor.  Reserves address space, but does not commit any memory. */
    [[nodiscard]] address_space() noexcept(!DEBUG) :
        m_ptr(reinterpret_cast<pointer>(impl::reserve_address_space(nbytes())))
    {
        if constexpr (DEBUG) {
            if (m_ptr == nullptr) {
                throw MemoryError(std::format(
                    "failed to reserve virtual address space of size {:.2e} MiB",
                    double(nbytes()) / double(impl::bytes::MiB)
                ));
            }
        }
    }

    address_space(const address_space&) = delete;
    address_space& operator=(const address_space&) = delete;

    address_space(address_space&& other) noexcept : m_ptr(other.m_ptr) {
        other.m_ptr = nullptr;
    }

    address_space& operator=(address_space&& other) noexcept(!DEBUG) {
        if (this != &other) {
            if (m_ptr) {
                bool rc = impl::free_address_space(m_ptr, nbytes());
                if constexpr (DEBUG) {
                    if (rc) {
                        throw MemoryError(std::format(
                            "failed to unreserve virtual address space starting "
                            "at address {:#x} and ending at address {:#x} ({:.2e} MiB)",
                            reinterpret_cast<uintptr_t>(m_ptr),
                            reinterpret_cast<uintptr_t>(m_ptr) + nbytes(),
                            double(nbytes()) / double(impl::bytes::MiB)
                        ));
                    }
                }
            }
            m_ptr = other.m_ptr;
            other.m_ptr = nullptr;
        }
        return *this;
    }

    /* Unreserve the address space upon destruction, allowing the operating system to
    reuse the reserved addresses for future allocations. */
    ~address_space() noexcept(!DEBUG) {
        if (m_ptr) {
            bool rc = impl::free_address_space(m_ptr, nbytes());
            if constexpr (DEBUG) {
                if (rc) {
                    throw MemoryError(std::format(
                        "failed to unreserve virtual address space starting at "
                        "address {:#x} and ending at address {:#x} ({:.2e} MiB)",
                        reinterpret_cast<uintptr_t>(m_ptr),
                        reinterpret_cast<uintptr_t>(m_ptr) + nbytes(),
                        double(nbytes()) / double(impl::bytes::MiB)
                    ));
                }
            }
            m_ptr = nullptr;
        }
    }

    /* True if the address space was properly initialized and is not empty.  False
    otherwise. */
    [[nodiscard]] explicit operator bool() const noexcept { return m_ptr; }

    /* Return the maximum size of the virtual address space in units of `T`.  If
    `T` is void, then this refers to the total size (in bytes) of the address space
    itself. */
    [[nodiscard]] static constexpr size_type size() noexcept { return N; }

    /* Return the maximum size of the address space in bytes.  If `T` is void, then
    this is equivalent to `size()`. */
    [[nodiscard]] static constexpr size_type nbytes() noexcept { return N * sizeof(value_type); }

    /* Get a pointer to the beginning of the reserved address space.  If `T` is void,
    then the pointer will be returned as `std::byte*`. */
    [[nodiscard]] pointer data() noexcept { return m_ptr; }
    [[nodiscard]] const_pointer data() const noexcept { return m_ptr; }

    /* Iterate over the reserved addresses.  If `T` is void, then each element is
    represented as a `std::byte`. */
    [[nodiscard]] iterator begin() noexcept { return m_ptr; }
    [[nodiscard]] const_iterator begin() const noexcept { return m_ptr; }
    [[nodiscard]] const_iterator cbegin() const noexcept { return m_ptr;}
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
    offsets for the relevant syscall.  The result is either a pointer to the start of
    the committed memory, or `nullptr` if the commit failed in some (unspecified) way.

    Individual arena implementations are expected to wrap this method to make the
    interface more convenient.  This simply abstracts the low-level OS hooks to make
    them cross-platform. */
    [[nodiscard, gnu::malloc]] pointer allocate(
        size_type offset,
        size_type length
    ) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (offset + length > N) {
                throw MemoryError(std::format(
                    "attempted to commit memory at offset {} with length {}, "
                    "which exceeds the size of the virtual address space starting "
                    "at address {:#x} and ending at address {:#x} ({:.2e} MiB)",
                    offset,
                    length,
                    reinterpret_cast<uintptr_t>(m_ptr),
                    reinterpret_cast<uintptr_t>(m_ptr) + N,
                    double(N) / double(impl::bytes::MiB)
                ));
            }
        }
        offset *= sizeof(value_type);
        length *= sizeof(value_type);
        return reinterpret_cast<iterator>(
            impl::commit_address_space(m_ptr, offset, length)
        );
    }

    /* Manually release physical memory from the address space.  If `T` is not void,
    then `offset` and `length` are both multiplied by `sizeof(T)` to determine the
    actual offsets for the relevant syscall.  The result is false if an (unspecified)
    error occurred during the release operation.  Note that this does not unreserve
    addresses from the space itself, merely the physical memory backing them.

    Individual arena implementations are expected to wrap this method to make the
    interface more convenient.  This simply abstracts the low-level OS hooks to make
    them cross-platform.  Note that this does not unreserve the address space itself,
    which is done automatically by the destructor. */
    [[nodiscard]] bool deallocate(size_type offset, size_type length) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (offset + length > N) {
                throw MemoryError(std::format(
                    "attempted to decommit memory at offset {} with length {}, "
                    "which exceeds the size of the virtual address space starting "
                    "at address {:#x} and ending at address {:#x} ({:.2e} MiB)",
                    offset,
                    length,
                    reinterpret_cast<uintptr_t>(m_ptr),
                    reinterpret_cast<uintptr_t>(m_ptr) + N,
                    double(N) / double(impl::bytes::MiB)
                ));
            }
        }
        offset *= sizeof(value_type);
        length *= sizeof(value_type);
        return impl::decommit_address_space(m_ptr, offset, length);
    }

    /* Construct a value that was allocated from this address space using placement
    new. */
    template <typename... Args>
        requires (!meta::is_void<T> && std::constructible_from<T, Args...>)
    void construct(pointer p, Args&&... args) noexcept(
        !DEBUG && noexcept(new (p) T(std::forward<Args>(args)...))
    ) {
        if constexpr (DEBUG) {
            if (p < begin() || p >= end()) {
                throw MemoryError(std::format(
                    "pointer at address {:#x} was not allocated from the virtual "
                    "address space starting at address {:#x} and ending at "
                    "address {:#x} ({:.2e} MiB)",
                    reinterpret_cast<uintptr_t>(p),
                    reinterpret_cast<uintptr_t>(m_ptr),
                    reinterpret_cast<uintptr_t>(m_ptr) + nbytes(),
                    double(nbytes()) / double(impl::bytes::MiB)
                ));
            }
        }
        new (p) T(std::forward<Args>(args)...);
    }

    /* Destroy a value that was allocated from this address space by calling its
    destructor in-place.  Note that this does not deallocate the memory itself.  This
    only occurs when the address space is destroyed, or when physical memory is
    explicitly decommitted using the `deallocate()` method. */
    void destroy(pointer p) noexcept(!DEBUG && noexcept(p->~T())) {
        if constexpr (DEBUG) {
            if (p < begin() || p >= end()) {
                throw MemoryError(std::format(
                    "pointer at address {:#x} was not allocated from the virtual "
                    "address space starting at address {:#x} and ending at "
                    "address {:#x} ({:.2e} MiB)",
                    reinterpret_cast<uintptr_t>(p),
                    reinterpret_cast<uintptr_t>(m_ptr),
                    reinterpret_cast<uintptr_t>(m_ptr) + nbytes(),
                    double(nbytes()) / double(impl::bytes::MiB)
                ));
            }
        }
        p->~T();
    }

private:
    pointer m_ptr;
};


/* A specialization of `address_space<T>` whose capacity is only known at runtime.
Such spaces have all the same characteristics as a statically-sized address space, but
can potentially be extended to larger sizes if necessary, as long as no other
allocations have been made within the extended region. */
template <meta::unqualified T>
struct address_space<T, 0> : impl::address_space_tag {
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
    };

    /* The default capacity to reserve if no template override is given.  This defaults
    to ~32 MiB of total storage, evenly divided into contiguous segments of type T. */
    static constexpr size_type DEFAULT_CAPACITY = impl::DEFAULT_ADDRESS_CAPACITY<T>;

    /* False if the address space's total capacity can only be known at runtime.
    Otherwise, equal to `N`, which is the maximum capacity of the address space in
    units of `T`.  If `T` is void, then this refers to the total memory (in bytes) that
    are reserved for the address space. */
    static constexpr size_type STATIC = 0;

    /* Reserve enough address space to store `capacity` instances of type `T`.  If
    `T` is void, `capacity` refers to a raw number of bytes to reserve. */
    [[nodiscard]] address_space(size_type capacity = 0) noexcept(!DEBUG) :
        m_ptr(nullptr), m_capacity(capacity)
    {
        if (capacity) {
            m_ptr = impl::reserve_address_space(nbytes());
            if constexpr (!DEBUG) {
                if (m_ptr == nullptr) {
                    throw MemoryError(std::format(
                        "failed to reserve virtual address space of size {:.2e} MiB",
                        double(nbytes()) / double(impl::bytes::MiB)
                    ));
                }
            }
        }
    }

    address_space(const address_space&) = delete;
    address_space& operator=(const address_space&) = delete;

    address_space(address_space&& other) noexcept :
        m_ptr(other.m_ptr), m_capacity(other.m_capacity)
    {
        other.m_ptr = nullptr;
        other.m_capacity = 0;
    }

    address_space& operator=(address_space&& other) noexcept(!DEBUG) {
        if (this != &other) {
            if (m_ptr) {
                bool rc = impl::free_address_space(m_ptr, nbytes());
                if constexpr (DEBUG) {
                    if (rc) {
                        throw MemoryError(std::format(
                            "failed to unreserve virtual address space starting "
                            "at address {:#x} and ending at address {:#x} ({:.2e} MiB)",
                            reinterpret_cast<uintptr_t>(m_ptr),
                            reinterpret_cast<uintptr_t>(m_ptr) + nbytes(),
                            double(nbytes()) / double(impl::bytes::MiB)
                        ));
                    }
                }
            }
            m_ptr = other.m_ptr;
            m_capacity = other.m_capacity;
            other.m_ptr = nullptr;
            other.m_capacity = 0;
        }
        return *this;
    }

    /* Unreserve the address space upon destruction, allowing the operating system to
    reuse the reserved addresses for future allocations. */
    ~address_space() noexcept(!DEBUG) {
        if (m_ptr) {
            bool rc = impl::free_address_space(m_ptr, nbytes());
            if constexpr (DEBUG) {
                if (rc) {
                    throw MemoryError(std::format(
                        "failed to unreserve virtual address space starting at "
                        "address {:#x} and ending at address {:#x} ({:.2e} MiB)",
                        reinterpret_cast<uintptr_t>(m_ptr),
                        reinterpret_cast<uintptr_t>(m_ptr) + nbytes(),
                        double(nbytes()) / double(impl::bytes::MiB)
                    ));
                }
            }
            m_ptr = nullptr;
            m_capacity = 0;
        }
    }

    /* True if the address space was properly initialized and is not empty.  False
    otherwise. */
    [[nodiscard]] explicit operator bool() const noexcept { return m_ptr; }

    /* Return the maximum size of the virtual address space in units of `T`.  If
    `T` is void, then this refers to the total size (in bytes) of the address space
    itself. */
    [[nodiscard]] size_type size() const noexcept { return m_capacity; }

    /* Return the maximum size of the address space in bytes.  If `T` is void, then
    this is equivalent to `size()`. */
    [[nodiscard]] size_type nbytes() const noexcept { return m_capacity * sizeof(value_type); }

    /* Get a pointer to the beginning of the reserved address space.  If `T` is void,
    then the pointer will be returned as `std::byte*`. */
    [[nodiscard]] pointer data() noexcept { return m_ptr; }
    [[nodiscard]] const_pointer data() const noexcept { return m_ptr; }

    /* Iterate over the reserved addresses.  If `T` is void, then each element is
    represented as a `std::byte`. */
    [[nodiscard]] iterator begin() noexcept { return m_ptr; }
    [[nodiscard]] const_iterator begin() const noexcept { return m_ptr; }
    [[nodiscard]] const_iterator cbegin() const noexcept { return m_ptr;}
    [[nodiscard]] iterator end() noexcept { return begin() + m_capacity; }
    [[nodiscard]] const_iterator end() const noexcept { return begin() + m_capacity; }
    [[nodiscard]] const_iterator cend() const noexcept { return begin() + m_capacity; }
    [[nodiscard]] reverse_iterator rbegin() noexcept { return {end()}; }
    [[nodiscard]] const_reverse_iterator rbegin() const noexcept { return {end()}; }
    [[nodiscard]] const_reverse_iterator crbegin() const noexcept { return {end()}; }
    [[nodiscard]] reverse_iterator rend() noexcept { return {begin()}; }
    [[nodiscard]] const_reverse_iterator rend() const noexcept { return {begin()}; }
    [[nodiscard]] const_reverse_iterator crend() const noexcept { return {begin()}; }

    /* Manually commit physical memory to the address space.  If `T` is not void,
    `offset` and `length` are both multiplied by `sizeof(T)` to determine the actual
    offsets for the relevant syscall.  The result is either a pointer to the start
    of the committed memory, or `nullptr` if the commit failed in some (unspacified)
    way.

    Individual arena implementations are expected to wrap this method to make the
    interface more convenient.  This simply abstracts the low-level OS hooks to make
    them cross-platform. */
    [[nodiscard, gnu::malloc]] pointer allocate(
        size_type offset,
        size_type length
    ) noexcept (!DEBUG) {
        if constexpr (DEBUG) {
            if (offset + length > m_capacity) {
                throw MemoryError(std::format(
                    "attempted to commit memory at offset {} with length {}, "
                    "which exceeds the size of the virtual address space starting "
                    "at address {:#x} and ending at address {:#x} ({:.2e} MiB)",
                    offset,
                    length,
                    reinterpret_cast<uintptr_t>(m_ptr),
                    reinterpret_cast<uintptr_t>(m_ptr) + m_capacity,
                    double(m_capacity) / double(impl::bytes::MiB)
                ));
            }
        }
        offset *= sizeof(value_type);
        length *= sizeof(value_type);
        return reinterpret_cast<iterator>(
            impl::commit_address_space(m_ptr, offset, length)
        );
    }

    /* Manually release physical memory from the address space.  If `T` is not void,
    `offset` and `length` are both multiplied by `sizeof(T)` to determine the actual
    offsets for the relevant syscall.  The result is `true` if an (unspecified) error
    occurred during the release operation.  Note that this does not unreserve addresses
    from the space itself, merely the physical memory backing them.
    
    Individual arena implementations are expected to wrap this method to make the
    interface more convenient.  This simply abstracts the low-level OS hooks to make
    them cross-platform.  Note that this does not unreserve the address space itself,
    which is done automatically by the destructor. */
    [[nodiscard]] bool deallocate(size_type offset, size_type length) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (offset + length > m_capacity) {
                throw MemoryError(std::format(
                    "attempted to decommit memory at offset {} with length {}, "
                    "which exceeds the size of the virtual address space starting "
                    "at address {:#x} and ending at address {:#x} ({:.2e} MiB)",
                    offset,
                    length,
                    reinterpret_cast<uintptr_t>(m_ptr),
                    reinterpret_cast<uintptr_t>(m_ptr) + m_capacity,
                    double(m_capacity) / double(impl::bytes::MiB)
                ));
            }
        }
        offset *= sizeof(value_type);
        length *= sizeof(value_type);
        return impl::decommit_address_space(m_ptr, offset, length);
    }

    /* Attempt to extend the reserved address space in-place.  Returns true if the
    extension was successful, or false if the resize requires a relocation.  Note that
    in the latter case, the address space will not be extended, and it is up to the
    user to manually relocate the elements as needed.

    `capacity` is interpreted according to the same semantics as the constructor.

    Use of this method is generally discouraged, as one of the main benefits of virtual
    address spaces is as a guard against pointer invalidation.  If this method fails,
    then the only way to extend the address space is to copy/move the existing elements
    into a new space, potentially leaving dangling pointers and memory leaks to the
    original values.  It is therefore always preferable to just allocate a larger
    address space from the beginning, and never attempt to resize it. */
    [[nodiscard]] bool resize(size_type capacity) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (capacity < m_capacity) {
                throw MemoryError(std::format(
                    "new capacity {} is less than current capacity {} for address "
                    "space starting at address {:#x} and ending at address {:#x} "
                    "({:.2e} MiB)",
                    capacity,
                    m_capacity,
                    reinterpret_cast<uintptr_t>(m_ptr),
                    reinterpret_cast<uintptr_t>(m_ptr) + m_capacity,
                    double(m_capacity) / double(impl::bytes::MiB)
                ));
            }
        }

        void* ptr = reinterpret_cast<char*>(m_ptr) + nbytes();
        size_type delta = (capacity - m_capacity) * sizeof(value_type);

        #ifdef _WIN32
            void* temp = VirtualAlloc(
                ptr,
                delta,
                MEM_RESERVE,
                PAGE_NOACCESS
            );
            if (temp == nullptr || temp != ptr) {
                return false;
            }
        #elifdef __unix__
            #ifdef MAP_NORESERVE
                constexpr int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
            #else
                constexpr int flags = MAP_PRIVATE | MAP_ANONYMOUS;
            #endif
            void* temp = mmap(
                ptr,
                delta,
                PROT_NONE,  // no read/write access
                flags,
                -1,  // no file descriptor
                0  // no offset
            );
            if (temp == MAP_FAILED || temp != ptr) {
                return false;
            }
        #else
            return false;  // not supported
        #endif

        m_capacity = capacity;
        return true;
    }

    /* Construct a value that was allocated from this address space using placement
    new. */
    template <typename... Args>
        requires (!meta::is_void<T> && std::constructible_from<T, Args...>)
    void construct(pointer p, Args&&... args) noexcept(
        !DEBUG && noexcept(new (p) T(std::forward<Args>(args)...))
    ) {
        if constexpr (DEBUG) {
            if (p < begin() || p >= end()) {
                throw MemoryError(std::format(
                    "pointer at address {:#x} was not allocated from the virtual "
                    "address space starting at address {:#x} and ending at "
                    "address {:#x} ({:.2e} MiB)",
                    reinterpret_cast<uintptr_t>(p),
                    reinterpret_cast<uintptr_t>(m_ptr),
                    reinterpret_cast<uintptr_t>(m_ptr) + nbytes(),
                    double(nbytes()) / double(impl::bytes::MiB)
                ));
            }
        }
        new (p) T(std::forward<Args>(args)...);
    }

    /* Destroy a value that was allocated from this address space by calling its
    destructor in-place.  Note that this does not deallocate the memory itself.  This
    only occurs when the address space is destroyed, or when physical memory is
    explicitly decommitted using the `deallocate()` method. */
    void destroy(pointer p) noexcept(!DEBUG && noexcept(p->~T())) {
        if constexpr (DEBUG) {
            if (p < begin() || p >= end()) {
                throw MemoryError(std::format(
                    "pointer at address {:#x} was not allocated from the virtual "
                    "address space starting at address {:#x} and ending at "
                    "address {:#x} ({:.2e} MiB)",
                    reinterpret_cast<uintptr_t>(p),
                    reinterpret_cast<uintptr_t>(m_ptr),
                    reinterpret_cast<uintptr_t>(m_ptr) + nbytes(),
                    double(nbytes()) / double(impl::bytes::MiB)
                ));
            }
        }
        p->~T();
    }

private:
    pointer m_ptr;
    size_type m_capacity;
};


/* A contiguous array of uninitialized values of type `T`.

This is mostly meant as an alternative to `address_space<T, N>` for small `N`, and uses
the same interface.  Rather than exploiting virtual memory, this allocates all of the
reserved addresses ahead of time as a raw array, potentially resulting in higher
performance at the cost of capacity and flexibility.

Because they are allocated as a single block of memory, physical spaces are potentially
dangerous to use for large data structures, especially if placed on the stack (due to
buffer overflows).  Care should be taken to leave such spaces relatively small, and to
offload them to the heap or nest them within a virtual address space if necessary. */
template <meta::unqualified T, size_t N>
struct physical_space : impl::physical_space_tag {
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
    };

    /* Default constructor.  Allocates uninitialized storage equal to the size of the
    physical space. */
    [[nodiscard]] physical_space() noexcept = default;

    /// TODO: copy/move constructors?  Do these even make sense for physical spaces?

    // physical_space(const physical_space&) = delete;
    // physical_space& operator=(const physical_space&) = delete;

    /* True if the physical space is not empty.  False otherwise. */
    explicit operator bool() const noexcept { return N; }

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
    [[nodiscard]] iterator begin() noexcept { return reinterpret_cast<iterator>(m_storage); }
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
    [[nodiscard, gnu::malloc]] pointer allocate(
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
        offset *= sizeof(value_type);
        length *= sizeof(value_type);
        return reinterpret_cast<iterator>(m_storage + offset);
    }

    /* Zero out the physical memory associated with the given range.  If `T` is not
    void, then `offset` and `length` are both multiplied by `sizeof(T)` to determine
    the actual offsets. */
    [[nodiscard]] bool deallocate(size_type offset, size_type length) noexcept(!DEBUG) {
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
        offset *= sizeof(value_type);
        length *= sizeof(value_type);
        std::memset(m_storage + offset, 0, length);
        return true;
    }

    /* Construct a value that was allocated from this physical space using placement
    new. */
    template <typename... Args>
        requires (!meta::is_void<T> && std::constructible_from<T, Args...>)
    void construct(pointer p, Args&&... args) noexcept(
        !DEBUG && noexcept(new (p) T(std::forward<Args>(args)...))
    ) {
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
    destructor in-place.  Note that this does not deallocate the memory itself.  This
    only occurs when the physical space is destroyed. */
    void destroy(pointer p) noexcept(!DEBUG && noexcept(p->~T())) {
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
        p->~T();
    }

private:
    alignas(T) unsigned char m_storage[sizeof(value_type) * N];
};


}  // namespace bertrand


#endif  // BERTRAND_ALLOCATE_H
