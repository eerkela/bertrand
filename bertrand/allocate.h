#ifndef BERTRAND_ALLOCATE_H
#define BERTRAND_ALLOCATE_H

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
    constexpr size_t DEFAULT_ADDRESS_CAPACITY = (bytes::MiB + sizeof(T) - 1) / sizeof(T);
    template <meta::is_void T>
    constexpr size_t DEFAULT_ADDRESS_CAPACITY<T> = bytes::MiB;

}


#ifdef BERTRAND_ARENA_SIZE
    constexpr size_t ARENA_SIZE = BERTRAND_ARENA_SIZE;
#else
    constexpr size_t ARENA_SIZE = impl::bytes::TiB;
#endif


#ifdef BERTRAND_NUM_ARENAS
    constexpr size_t NUM_ARENAS = BERTRAND_NUM_ARENAS;
#else
    constexpr size_t NUM_ARENAS = 8;
#endif


template <meta::unqualified T, size_t N = impl::DEFAULT_ADDRESS_CAPACITY<T>>
struct address_space;


namespace impl {
    struct address_space_tag {};
    struct physical_space_tag {};

    /* A pool of virtual address spaces backing the `address_space<T, N>` constructor.
    These spaces are allocated upfront at startup and reused in a manner similar to
    a `malloc()`/`free()`-based heap, in order to greatly reduce syscall overhead when
    apportioning address spaces in downstream code.  Provides thread-safe access to
    each arena, and implements a simple load-balancing algorithm to ensure that the
    arenas are evenly utilized. */
    struct global_address_space {
    private:
        inline static thread_local std::mt19937 rng{std::random_device()()};
        inline static thread_local std::uniform_int_distribution<size_t> dist{
            0,
            NUM_ARENAS - 1
        };

        /* An individual arena, representing a contiguous virtual address space of length
        `ARENA_SIZE`.  The first GiB is reserved to store the available chunk list in
        contiguous memory */
        struct arena {
        private:
            template <typename T, size_t N>
            friend struct address_space;
            friend global_address_space;

            /// TODO: eventually swap to a balanced binary search tree

            struct Node {
                /// TODO: per-arena free list
            };

            mutable std::mutex m_mutex;
            size_t m_id;
            size_t m_used = 0;
            size_t m_offset = 0;
            Node* m_freelist = nullptr;
            std::byte* m_data;

            arena(size_t id) : m_id(id), m_data([] {
                std::byte* result = nullptr;
                #ifdef _WIN32
                    result = reinterpret_cast<std::byte*>(VirtualAlloc(
                        nullptr,
                        ARENA_SIZE,
                        MEM_RESERVE,
                        PAGE_NOACCESS
                    ));
                #elifdef __unix__
                    #ifdef MAP_NORESERVE
                        constexpr int flags =
                            MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE;
                    #else
                        constexpr int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED;
                    #endif
                    void* ptr = mmap(
                        nullptr,
                        ARENA_SIZE,
                        PROT_NONE,  // reserve only, do not allocate pages
                        flags,
                        -1,  // no file descriptor
                        0  // no offset
                    );
                    result = ptr == MAP_FAILED ?
                        nullptr : reinterpret_cast<std::byte*>(ptr);
                #endif
                return result + impl::bytes::GiB;
            }()) {}

            /// TODO: allocate() expects the arena mutex to be locked, and unlocks it
            /// when finished.  Deallocate() will both lock and unlock the mutex in
            /// the same call.  Then, allocate() will be called in the address space
            /// constructor, and deallocate() will be called in the destructor.

            [[nodiscard]] std::byte* allocate(size_t capacity) {

            }

            [[nodiscard]] bool deallocate(std::byte* ptr, size_t capacity) {

            }

        public:

            /* The ID of this arena, which is always an integer from
            [0, NUM_ARENAS). */
            [[nodiscard]] size_t id() const noexcept { return m_id; }

            /* The number of bytes that have been used in this arena.  This is always
            less than or equal to `capacity()`. */
            [[nodiscard]] size_t size() const noexcept { return m_used; }

            /* The total amount of virtual memory that is stored in this arena.  This
            is a compile-time constant that is set by the `BERTRAND_ARENA_SIZE` build
            flag.  More capacity allows for a greater number and size of child address
            spaces, but can compete with the rest of the program's virtual address
            space if set too high.  Note that this does not refer to actual physical
            memory, just a range of pointers that are reserved for each arena. */
            [[nodiscard]] size_t capacity() const noexcept { return ARENA_SIZE; }

            /* A pointer to the beginning of this arena's address range, returned as
            a `std::byte*` pointer.  Pointer arithmetic can be used to navigate to
            specific sections of the address space. */
            [[nodiscard]] const std::byte* data() const noexcept { return m_data; }

            ~arena() noexcept {
                /// NOTE: destructor is only called at program termination, so errors
                /// here are not critical.  The OS will reclaim memory anyways unless
                /// something is really wrong.
                #ifdef _WIN32
                    if (VirtualFree(m_ptr, 0, MEM_RELEASE) != 0) {}
                #elifdef __unix__
                    if (munmap(m_data, ARENA_SIZE) == 0) {}
                #endif
            }
        };

        /* Arenas are stored in an array where each index is equal to the arena's ID. */
        std::array<arena, NUM_ARENAS> m_arenas =
            []<size_t... Is>(std::index_sequence<Is...>) -> std::array<arena, NUM_ARENAS> {
                return {arena{Is}...};
            }(std::make_index_sequence<NUM_ARENAS>{});

    public:

        /* The number of arenas in the global address space.  This is a compile-time
        constant that is set by the `BERTRAND_NUM_ARENAS` build flag, and defaults to 8
        if not specified.  More arenas may reduce contention in multithreaded
        environments. */
        [[nodiscard]] static constexpr size_t size() noexcept { return NUM_ARENAS; }

        /* Access a specific arena by its ID, which is a sequential integer from 0 to
        size. */
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

        /* Search for an available arena for a requested address space of size
        `capacity`.  A null pointer will be returned if all arenas are full.
        Otherwise, the arena will be returned in a locked state, requiring the user to
        manually unlock its mutex once they are finished with the insertion. */
        [[nodiscard]] arena* acquire(size_t capacity) noexcept {
            if constexpr (NUM_ARENAS == 0) {
                return nullptr;

            } else {
                struct Node {
                    arena* ptr;
                    Node* next = nullptr;
                    bool full = false;
                };

                constexpr size_t max_neighbors = 4;
                constexpr size_t neighbors =
                    max_neighbors < NUM_ARENAS ? max_neighbors : NUM_ARENAS;

                // concurrent load balancing is performed stochastically, by generating a
                // random index into the arena array and building a neighborhood of the
                // next 4 as a ring buffer sorted by utilization.
                size_t index = 0;
                if constexpr (NUM_ARENAS > max_neighbors) {
                    index = dist(rng) % NUM_ARENAS;
                }

                std::array<Node, neighbors> nodes = []<size_t... Is>(
                    std::index_sequence<Is...>,
                    size_t index,
                    arena* arenas
                ) -> std::array<Node, neighbors> {
                    return {Node{&arenas[(index + Is) % NUM_ARENAS]}...};
                }(std::make_index_sequence<neighbors>{}, index, m_arenas.data());

                Node* head = &nodes[0];
                Node* tail = &nodes[0];
                for (size_t i = 1; i < neighbors; ++i) {
                    Node& node = nodes[i];
                    if (node.ptr->size() < head->ptr->size()) {
                        node.next = head;
                        head = &node;
                    } else if (node.ptr->size() >= tail->ptr->size()) {
                        tail->next = &node;
                        tail = &node;
                    } else {
                        Node* prev = head;
                        Node* curr = head->next;
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
                return nullptr;
            }
        }
    };


    /* Backs the `address_space<T>` constructor.  `size` is interpreted as a raw
    number of bytes.  Returns null if the allocation failed. */
    inline void* reserve_address_space(size_t size) noexcept {
        #ifdef _WIN32
            return VirtualAlloc(
                nullptr,
                size,
                MEM_RESERVE,
                PAGE_NOACCESS
            );
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
                PROT_NONE,  // no read/write access
                flags,
                -1,  // no file descriptor
                0  // no offset
            );
            return ptr == MAP_FAILED ? nullptr : ptr;
        #else
            return nullptr;  // not supported
        #endif
    }

    /* Backs the `address_space<T>` destructor.  `ptr` is a pointer returned by
    `reserve_address_space()` and `size` must be equal to the size used to construct
    it.  Returns `true` to indicate an error. */
    inline bool free_address_space(void* ptr, size_t size) noexcept {
        #ifdef _WIN32
            return VirtualFree(ptr, 0, MEM_RELEASE) != 0;
        #elifdef __unix__
            return munmap(ptr, size) == 0;
        #endif
    }

    /* Backs the `address_space<T>.allocate()` method.  `ptr` is a pointer returned by
    `reserve_address_space()` and `offset` and `size` are interpreted as a raw number
    of bytes into that address. */
    [[gnu::malloc]] inline void* commit_address_space(
        void* ptr,
        size_t offset,
        size_t length
    ) noexcept {
        void* p = reinterpret_cast<char*>(ptr) + offset;
        #ifdef _WIN32
            LPVOID result = VirtualAlloc(
                reinterpret_cast<LPVOID>(p),
                length,
                MEM_COMMIT,
                PAGE_READWRITE
            );
            return reinterpret_cast<void*>(result);
        #elifdef __unix__
            int rc = mprotect(
                p,
                length,
                PROT_READ | PROT_WRITE
            );
            if (rc != 0) {
                return nullptr;
            }
            return p;
        #else
            return nullptr;  // not supported
        #endif
    }

    /* Backs the `address_space<T>.deallocate()` method.  `ptr` is a pointer returned
    by `reserve_address_space()` and `offset` and `size` are interpreted as a raw
    number of bytes into that address. */
    inline bool decommit_address_space(void* ptr, size_t offset, size_t length) noexcept {
        void* p = reinterpret_cast<char*>(ptr) + offset;
        #ifdef _WIN32
            return VirtualFree(
                reinterpret_cast<LPVOID>(p),
                length,
                MEM_DECOMMIT
            ) == 0;
        #elifdef __unix__
            return mprotect(
                p,
                length,
                PROT_NONE
            ) != 0;
        #else
            return true;  // not supported
        #endif
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


/* The page size for the current system in bytes.  If the page size could not be
determined, then this evaluates to zero. */
inline size_t PAGE_SIZE = [] {
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
inline bool HAS_ADDRESS_SPACE = [] {
    if (PAGE_SIZE == 0) {
        return false;
    }
    void* ptr = impl::reserve_address_space(PAGE_SIZE);
    if (ptr == nullptr) {
        return false;
    }
    return !impl::free_address_space(ptr, PAGE_SIZE);
}();


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
