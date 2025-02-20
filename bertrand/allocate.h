#ifndef BERTRAND_ALLOCATE_H
#define BERTRAND_ALLOCATE_H


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
    struct virtual_address_tag {};

    /* Backs the `virtual_address<T>` constructor.  `size` is interpreted as a raw
    number of bytes.  Returns null if the allocation failed. */
    inline void* reserve_virtual_address(size_t size) {
        #ifdef _WIN32
            return VirtualAlloc(
                nullptr,
                size,
                MEM_RESERVE,
                PAGE_NOACCESS
            );
        #elifdef __unix__
            #ifdef MAP_NORESERVE
                constexpr int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;
            #else
                constexpr int flags = MAP_PRIVATE | MAP_ANONYMOUS;
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
            return nullptr;
        #endif
    }

    /* Backs the `virtual_address<T>` destructor.  `ptr` is a pointer returned by
    `reserve_virtual_address()` and `size` must be equal to the size used to construct
    it.  Returns `true` to indicate an error. */
    inline bool free_virtual_address(void* ptr, size_t size) {
        #ifdef _WIN32
            return VirtualFree(ptr, 0, MEM_RELEASE) != 0;
        #elifdef __unix__
            return munmap(ptr, size) == 0;
        #endif
    }

    namespace bytes {
        enum : size_t {
            B = 1,
            KiB = 1024,
            MiB = 1024 * KiB,
            GiB = 1024 * MiB
        };
    }

}


namespace meta {

    template <typename T>
    concept virtual_address = inherits<T, impl::virtual_address_tag>;

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
inline bool SUPPORTS_VIRTUAL_ADDRESS = [] {
    if (PAGE_SIZE == 0) {
        return false;
    }
    void* ptr = impl::reserve_virtual_address(PAGE_SIZE);
    if (ptr == nullptr) {
        return false;
    }
    return !impl::free_virtual_address(ptr, PAGE_SIZE);
}();


/* A contiguous space of reserved virtual addresses (pointers), into which physical
memory can be allocated.

Note that this does not allocate memory by itself.  Instead, it denotes a range of
forbidden pointer values that the operating system will not assign to any other source
within the same process.  Physical memory can then be requested to back these addresses
as needed, which will cause the operating system to allocate physical memory and map it
into the reserved virtual address space using the CPU's Memory Management Unit (MMU).
Such hardware is not guaranteed to be present on all systems (particularly for embedded
systems), in which case this class will simply do nothing, and will always evaluate to
`false` under boolean logic to indicate an empty address space.  Otherwise, the address
space will have been reserved, and any physical pages assigned to it will be
automatically released when the address space is destroyed.

The template parameter `T` can be used to specify the type of data that will be stored
in the address space, defaulting to `void`.  The address space itself is held as a `T*`
pointer, and the size used to construct it indicates how many instances of `T` can be
stored within the space (e.g. `virtual_address<Foo>(5)` reserves enough space for 5
complete instances of `Foo`).  If `T = void`, then the size is interpreted as a raw
number of bytes, which can be used to store arbitrary data.

This class is primarily used to implement low-level memory allocation for arena
allocators and dynamic data structures, such as high-performance vectors.  Since the
virtual address space is reserved in advance and decoupled from physical memory, it is
possible to implement these data structures without ever relocating objects in memory,
which is ordinarily a costly operation that results in memory fragmentation and
possible dangling pointers.  By using a virtual address space, the data structure can
be trivially resized by simply allocating additional pages of physical memory, without
changing the addresses of existing objects.  This improves both performance and safety,
since there is no risk of pointer invalidation or memory leaks due to growth of the
data structure itself (although invalidation can still occur if relocation occurs for
other reasons, such as by removing an element from the middle of a vector, for
example).

Address spaces are not thread-safe by default, and must be protected by a lock if
multiple threads are expected to access them concurrently. */
template <meta::unqualified T, size_t N>
struct virtual_address : impl::virtual_address_tag {
private:
    T* m_ptr;

public:
    using type = T;
    enum : size_t {
        B = impl::bytes::B,
        KiB = impl::bytes::KiB,
        MiB = impl::bytes::MiB,
        GiB = impl::bytes::GiB,
    };

    /* Default constructor.  Reserves address space, but does not commit any memory. */
    [[nodiscard]] virtual_address() noexcept(!DEBUG) {
        if constexpr (meta::is_void<T>) {
            m_ptr = impl::reserve_virtual_address(N);
        } else {
            m_ptr = impl::reserve_virtual_address(N * sizeof(T));
        }
        if constexpr (DEBUG) {
            if (m_ptr == nullptr) {
                double capacity = N;
                if constexpr (!meta::is_void<T>) {
                    capacity *= sizeof(T);
                }
                throw MemoryError(std::format(
                    "Failed to reserve virtual address space of size {:.2e} MiB",
                    capacity / double(impl::bytes::MiB)
                ));
            }
        }
    }

    virtual_address(const virtual_address&) = delete;
    virtual_address& operator=(const virtual_address&) = delete;

    virtual_address(virtual_address&& other) noexcept : m_ptr(other.m_ptr) {
        other.m_ptr = nullptr;
    }
    virtual_address& operator=(virtual_address&& other) noexcept(!DEBUG) {
        if (this != &other) {
            if (m_ptr) {
                size_t size = N;
                if constexpr (!meta::is_void<T>) {
                    size *= sizeof(T);
                }
                bool rc = impl::free_virtual_address(m_ptr, size);
                if constexpr (DEBUG) {
                    if (rc) {
                        throw MemoryError(std::format(
                            "Failed to unreserve virtual address space starting "
                            "at address {:#x} and ending at address {:#x} ({:.2e} MiB)",
                            reinterpret_cast<uintptr_t>(m_ptr),
                            reinterpret_cast<uintptr_t>(m_ptr) + size,
                            double(size) / double(impl::bytes::MiB)
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
    ~virtual_address() noexcept(!DEBUG) {
        if (m_ptr) {
            size_t size = N;
            if constexpr (!meta::is_void<T>) {
                size *= sizeof(T);
            }
            bool rc = impl::free_virtual_address(m_ptr, size);
            if constexpr (DEBUG) {
                if (rc) {
                    throw MemoryError(std::format(
                        "Failed to unreserve virtual address space starting at "
                        "address {:#x} and ending at address {:#x} ({:.2e} MiB)",
                        reinterpret_cast<uintptr_t>(m_ptr),
                        reinterpret_cast<uintptr_t>(m_ptr) + size,
                        double(size) / double(impl::bytes::MiB)
                    ));
                }
            }
            m_ptr = nullptr;
        }
    }

    /* True if the address space was successfully reserved.  False otherwise.  This is
    typically used to detect errors in the constructor in a style similar to
    `malloc()`. */
    explicit operator bool() const noexcept { return m_ptr; }

    /* Return the maximum size of the virtual address space in units of `T`.  If
    `T` is void, then this refers to the total size (in bytes) of the address space
    itself. */
    [[nodiscard]] static constexpr size_t size() noexcept { return N; }

    [[nodiscard]] T* data() noexcept { return m_ptr; }
    [[nodiscard]] T* begin() noexcept { return m_ptr; }
    [[nodiscard]] T* end() noexcept { return m_ptr + N; }
    [[nodiscard]] const T* data() const noexcept { return m_ptr; }
    [[nodiscard]] const T* begin() const noexcept { return m_ptr; }
    [[nodiscard]] const T* end() const noexcept { return m_ptr + N; }

    /* Manually commit physical memory to the address space.  If `T` is not void,
    `offset` and `length` are both multiplied by `sizeof(T)` to determine the actual
    offsets for the relevant syscall.  The result is either a pointer to the start of
    the committed memory, or `nullptr` if the commit failed in some (unspecified) way.

    Individual arena implementations are expected to wrap this method to make the
    interface more convenient.  This simply abstracts the low-level OS hooks to make
    them cross-platform. */
    [[nodiscard]] T* commit(size_t offset, size_t length) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (offset + length > N) {
                throw MemoryError(std::format(
                    "Attempted to commit memory at offset {} with length {}, "
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
        if constexpr (!meta::is_void<T>) {
            offset *= sizeof(T);
            length *= sizeof(T);
        }
        void* ptr = reinterpret_cast<uintptr_t>(m_ptr) + offset;
        #ifdef _WIN32
            LPVOID result = VirtualAlloc(
                reinterpret_cast<LPVOID>(ptr),
                length,
                MEM_COMMIT,
                PAGE_READWRITE
            );
            return reinterpret_cast<T*>(result);
        #elifdef __unix__
            int rc = mprotect(
                ptr,
                length,
                PROT_READ | PROT_WRITE
            );
            if (rc != 0) {
                return nullptr;
            }
            return reinterpret_cast<T*>(ptr);
        #else
            return nullptr;  // not supported
        #endif
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
    [[nodiscard]] bool decommit(size_t offset, size_t length) noexcept(!DEBUG) {
        if constexpr (DEBUG) {
            if (offset + length > N) {
                throw MemoryError(std::format(
                    "Attempted to decommit memory at offset {} with length {}, "
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
        if constexpr (!meta::is_void<T>) {
            offset *= sizeof(T);
            length *= sizeof(T);
        }
        void* ptr = reinterpret_cast<uintptr_t>(m_ptr) + offset;
        #ifdef _WIN32
            return VirtualFree(
                reinterpret_cast<LPVOID>(ptr),
                length,
                MEM_DECOMMIT
            ) == 0;
        #elifdef __unix__
            return mprotect(
                ptr,
                length,
                PROT_NONE
            ) != 0;
        #else
            return true;  // not supported
        #endif
    }
};


}  // namespace bertrand


#endif  // BERTRAND_ALLOCATE_H
