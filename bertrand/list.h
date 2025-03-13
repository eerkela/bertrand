#ifndef BERTRAND_LIST_H
#define BERTRAND_LIST_H


#include "bertrand/common.h"
#include "bertrand/allocate.h"
#include "bertrand/static_str.h"


namespace bertrand {


/// TODO: I should still offer a version of this class that just uses ordinary heap
/// allocations rather than a virtual address space.  This always occurs when N == 0




/* A simple dynamic array that stores its contents in a contiguous virtual address
space, which can grow without reallocation.  Also replicates a Python-style list
interface in C++, and can be optionally sorted using a custom comparison function.

Typical dynamic array implementations (such as `std::vector<T>`) store their contents
on the heap, only allocating as much memory as they need, plus some excess to prevent
thrashing.  Once the available space is exhausted, the array must allocate a new block
of memory from the heap, move the contents of the old block to the new block, and then
free the old block.  This is a costly operation that leads to memory fragmentation,
pointer invalidation for existing contents, and unpredictable performance, with
frequent small stutters as the array grows.  These can be mitigated somewhat by proper
use of the `reserve()` method, but that is not always feasible, and `reserve()` can
still trigger reallocation if the array lacks sufficient capacity.

In contrast, this class avoids dynamic allocations entirely by giving each list its own
private arena, which consists of a (usually large) virtual address space that is fully
decoupled from physical memory.  Upon construction, each list will reserve a range of
virtual addresses (pointers) without any physical memory to back them, and then direct
the operating system to allocate pages on-demand as they are needed.  Because of this,
and because the available virtual address space is quite large on 64-bit systems (on
the order of hundreds of terabytes), it is possible to overallocate each list by a
significant margin without consuming an undue amount of physical memory.  Since each
list reserves far more pointers than it typically needs, growth is trivial, consisting
of a single syscall to map additional pages into the expanded address space, without
moving existing elements.  A syscall of this form can then be amortized by applying
geometric growth, and because each allocation occurs in multiples of the system's page
size (typically 4 KiB), growths should be very rare in practice.

This approach has a number of advantages.  First, by avoiding O(n) moves from one block
to another, performance remains consistent and predictable even for large arrays,
without overly stressing the memory subsystem.  Second, because no relocations occur,
any pointers to existing elements are guaranteed to remain valid even after growth,
which is not the case for traditional heap-allocated arrays, and is a common source of
hard-to-find bugs.  Note that pointer invalidation can still occur for other reasons,
including insertion or removal of items in the middle of the list, but these are much
easier to spot, whereas invalidation due to changes in capacity often are not.  Third,
because each list uses its own private arena and does not interact with the heap in any
way, there is no risk of heap corruption, fragmentation, leaks, or double frees, or any
other security issues that can arise from misusing the heap.  Additionally, since
virtual address spaces are stored out-of-line and consume relatively little stack space
by themselves, they are much more resistant to stack overflows compared to raw arrays
or `std::inplace_vector<T>`.  Lastly, it is trivial to place OS-level guard pages
around the list to detect out-of-bounds reads/writes, or to apply other hardening
techniques to further address security concerns.

The downsides to such an approach are as follows.  First, each list must have an
explicit maximum size beyond which it cannot grow, although the use of virtual memory
means this can be quite large, and practically unachievable under normal usage.  In
fact, this can be seen as an additional security feature, since it prevents unbounded
growth of the list, which can lead to denial-of-service attacks or exhaustion of the
address space.  Second, the minimum size of such a list is limited to the size of a
single page, which is often far more than is necessary for small, transitory lists.
This is mitigated by introducing a small space optimization for lists whose maximum
sizes are less than one full page in memory, which causes the space to be allocated as
an inline array, similar to `std::inplace_vector`.  Third, the use of virtual memory
relies on the presence of a Memory Management Unit (MMU) within the target architecture
to map virtual addresses to physical addresses, which is not guaranteed to be present
on all platforms.  If an MMU is not present, or the program is compiled without virtual
memory support, then the list will fall back to a traditional heap-based approach using
`malloc()` and `free()`, which foregoes the benefits listed above, but otherwise
functions similarly to a typical `std::vector<T>`.

The additional template parameters `Equal` and `Less` can be used to specify custom
comparison functions for elements within the list.  If `Less` is not void, then it must
be a default-constructible type that can be invoked to apply a less-than comparison
between two elements of the list, as well as any other types that may be transparently
compared.  This forces a strict ordering on the list itself, which will be maintained
during insertion, and allows for O(log(n)) binary searches for individual elements.
Otherwise, searches are strictly linear, using the `Equal` function to determine
equality. */
template <
    meta::not_void T,
    size_t N = impl::DEFAULT_ADDRESS_CAPACITY<T>,
    meta::unqualified Equal = std::equal_to<>,
    meta::unqualified Less = void
>
    requires (!meta::reference<T> && (
        meta::is_void<Equal> || (
            meta::default_constructible<Equal> &&
            meta::invoke_returns<
                bool,
                meta::as_lvalue<Equal>,
                meta::as_lvalue<meta::as_const<T>>,
                meta::as_lvalue<meta::as_const<T>>
            >
        )
    ) && (
        meta::is_void<Less> || (
            meta::default_constructible<Less> &&
            meta::invoke_returns<
                bool,
                meta::as_lvalue<Less>,
                meta::as_lvalue<meta::as_const<T>>,
                meta::as_lvalue<meta::as_const<T>>
            >
        )
    ))
struct List;


namespace impl {
    struct list_tag {};

    template <typename T, size_t N>
    struct physical_list_allocator {
        using size_type = size_t;
        using pointer = meta::as_pointer<T>;
        using const_pointer = meta::as_pointer<meta::as_const<T>>;

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
            meta::trivially_destructible<T> ||
            noexcept(space.destroy(space.data()))
        ) {
            if constexpr (meta::trivially_destructible<T>) {
                size = 0;
            } else {
                while (size) {
                    --size;
                    space.destroy(space.data() + size);
                }
            }
        }

        constexpr physical_list_allocator() = default;

        constexpr physical_list_allocator(const physical_list_allocator& other) noexcept(
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

        constexpr physical_list_allocator(physical_list_allocator&& other) noexcept(
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

        constexpr physical_list_allocator& operator=(
            const physical_list_allocator& other
        ) noexcept(
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

        constexpr physical_list_allocator& operator=(
            physical_list_allocator&& other
        ) noexcept(
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

        constexpr ~physical_list_allocator() noexcept(
            noexcept(clear()) && meta::nothrow::destructible<address_space<T, N>>
        ) {
            clear();
        }
    };

    template <typename T, size_t N>
    struct virtual_list_allocator {
        using size_type = size_t;
        using pointer = meta::as_pointer<T>;
        using const_pointer = meta::as_pointer<meta::as_const<T>>;

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
            meta::trivially_destructible<T> ||
            noexcept(space.destroy(space.data()))
        ) {
            if constexpr (meta::trivially_destructible<T>) {
                size = 0;
            } else {
                while (size) {
                    --size;
                    space.destroy(space.data() + size);
                }
            }
        }

        constexpr virtual_list_allocator() = default;

        constexpr virtual_list_allocator(const virtual_list_allocator& other) noexcept(
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

        constexpr virtual_list_allocator(virtual_list_allocator&& other) noexcept(
            noexcept(address_space<T, N>(std::move(other.space)))
        ) :
            capacity(other.capacity),
            size(other.size),
            space(std::move(other.space))
        {}

        constexpr virtual_list_allocator& operator=(
            const virtual_list_allocator& other
        ) noexcept(
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

        constexpr virtual_list_allocator& operator=(
            virtual_list_allocator&& other
        ) noexcept(
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

        constexpr ~virtual_list_allocator() noexcept(
            noexcept(clear()) &&
            meta::nothrow::destructible<address_space<T, N>>
        ) {
            clear();
        }
    };

    template <typename T, size_t N>
    struct heap_list_allocator {
        using size_type = size_t;
        using pointer = meta::as_pointer<T>;
        using const_pointer = meta::as_pointer<meta::as_const<T>>;

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
            meta::trivially_destructible<T> ||
            meta::nothrow::destructible<T>
        ) {
            if constexpr (!meta::trivially_destructible<T>) {
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
                        if constexpr (!meta::trivially_destructible<T>) {
                            storage[i++].~T();
                        }
                    } catch (...) {
                        for (size_type j = 0; j < i; ++j) {
                            new (storage + j) T(std::move(new_storage[j]));
                            if constexpr (!meta::trivially_destructible<T>) {
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
                        if constexpr (!meta::trivially_destructible<T>) {
                            storage[i++].~T();
                        }
                        
                    } catch (...) {
                        for (size_type j = 0; j < i; ++j) {
                            new (storage + j) T(std::move(new_storage[j]));
                            if constexpr (!meta::trivially_destructible<T>) {
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

        constexpr void clear() noexcept(
            meta::trivially_destructible<T> ||
            meta::nothrow::destructible<T>
        ) {
            if constexpr (meta::trivially_destructible<T>) {
                size = 0;
            } else {
                while (size) {
                    --size;
                    storage[size].~T();
                }
            }
        }

        constexpr heap_list_allocator() = default;

        constexpr heap_list_allocator(const heap_list_allocator& other) :
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

        constexpr heap_list_allocator(heap_list_allocator&& other) noexcept :
            size(other.size),
            capacity(other.capacity),
            storage(other.storage)
        {
            other.size = 0;
            other.capacity = 0;
            other.storage = nullptr;
        }

        constexpr heap_list_allocator& operator=(const heap_list_allocator& other) {
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

        constexpr heap_list_allocator& operator=(heap_list_allocator&& other) noexcept(
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

        constexpr ~heap_list_allocator() noexcept(
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

    template <typename T, size_t N, typename Equal, typename Less>
    struct list_base : list_tag {
        using equal_type = Equal;
        using less_type = Less;
        using size_type = size_t;
        using index_type = meta::as_signed<size_type>;
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using reference = meta::as_lvalue<value_type>;
        using const_reference = meta::as_lvalue<meta::as_const<value_type>>;
        using pointer = meta::as_pointer<value_type>;
        using const_pointer = meta::as_pointer<meta::as_const<value_type>>;
        using iterator = impl::contiguous_iterator<std::conditional_t<
            meta::is_void<Less>,
            value_type,
            meta::as_const<value_type>
        >>;
        using const_iterator = impl::contiguous_iterator<meta::as_const<value_type>>;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;
        using slice = impl::contiguous_slice<std::conditional_t<
            meta::is_void<Less>,
            value_type,
            meta::as_const<value_type>
        >>;
        using const_slice = impl::contiguous_slice<meta::as_const<value_type>>;

        /* The default capacity to reserve if no template override is given.  This defaults
        to a maximum of ~8 MiB of total storage, evenly divided into contiguous segments of
        type `T`. */
        static constexpr bool DEFAULT_CAPACITY = impl::DEFAULT_ADDRESS_CAPACITY<T>;

        /* True if the container is backed by a stable address space, meaning that dynamic
        growth will not invalidate pointers to existing elements.  This is equivalent to
        the `bertrand::HAS_ARENA` flag */
        static constexpr bool STABLE_ADDRESS = address_space<T, N>::SMALL || HAS_ARENA;

        /* True if the container is naturally sorted, which occurs when it is templated
        with a non-void `Less` function.  Such a container is guaranteed to always maintain
        strict ascending order based on the comparison function, allowing for O(log(n))
        binary searches using any key that can be transparently evaluated by the `Less`
        function (as if the STL's `is_transparent` property were always true). */
        static constexpr bool SORTED = meta::not_void<Less>;

    protected:

        /* Shift all elements from `pivot` to the end of the list to the right by
        `offset` indices.  If a move constructor fails, then a best-faith effort is
        made to return the shifted elements back to their original positions. */
        constexpr void shift_right(size_type pivot, size_type delta) {
            for (size_type i = size(); i-- > pivot;) {
                try {
                    m_alloc.construct(
                        m_alloc.data() + i + delta,
                        std::move(m_alloc.data()[i])
                    );
                } catch(...) {
                    shift_left(i + 1, delta);
                    throw;
                }
                m_alloc.destroy(m_alloc.data() + i);
            }
        }

        /* Shift all elements from `pivot` to the end of the list to the left by
        `offset` indices.  This is only used during error recovery for a failed
        insertion toward the middle of the list. */
        constexpr void shift_left(size_type pivot, size_type delta) {
            for (size_type i = pivot; i < size(); ++i) {
                m_alloc.construct(
                    m_alloc.data() + i,
                    std::move(m_alloc.data()[i + delta])
                );
                m_alloc.destroy(m_alloc.data() + i + delta);
            }
        }

        using alloc = std::conditional_t<
            address_space<T, N>::SMALL,
            impl::physical_list_allocator<T, N>,
            std::conditional_t<
                HAS_ARENA,
                impl::virtual_list_allocator<T, N>,
                impl::heap_list_allocator<T, N>
            >
        >;

        alloc m_alloc;

    public:
        /* Default constructor.  Creates an empty list and reserves virtual address space
        if `STABLE_ADDRESS == true`. */
        [[nodiscard]] constexpr list_base() noexcept(noexcept(alloc())) = default;

        /* Copy constructor.  Copies the contents of another list into a new address
        range. */
        [[nodiscard]] constexpr list_base(const list_base& other) noexcept(
            noexcept(alloc(other.m_alloc))
        ) = default;

        /* Move constructor.  Preserves the original addresses and simply transfers
        ownership if the list is stored on the heap or in a virtual address space.  If `N`
        is small enough to trigger the small space optimization, then the contents will be
        moved elementwise into the new list. */
        [[nodiscard]] constexpr list_base(list_base&& other) noexcept(
            noexcept(alloc(std::move(other.m_alloc)))
        ) = default;

        /* Copy assignment operator.  Dumps the current contents of the list and then
        copies those of another list into a new address range. */
        [[maybe_unused]] constexpr list_base& operator=(const list_base& other) noexcept(
            noexcept(m_alloc = other.m_alloc)
        ) = default;

        /* Move assignment operator.  Dumps the current contents of the list and then  */
        [[maybe_unused]] constexpr list_base& operator=(list_base&& other) noexcept(
            noexcept(m_alloc = std::move(other.m_alloc))
        ) = default;

        /* Destructor.  Calls destructors for each of the elements and then releases any
        memory held by the list.  If the list was backed by a virtual address space, then
        the address space will be released and any physical pages will be reclaimed by the
        operating system.  */
        constexpr ~list_base() noexcept(noexcept(m_alloc.~alloc())) = default;

        /* Swap two lists as cheaply as possible.  If an exception occurs, a best-faith
        effort is made to restore the operands to their original state. */
        constexpr void swap(list_base& other) & noexcept(
            noexcept(List<T, N, Equal, Less>(std::move(*this))) &&
            noexcept(*this = std::move(other)) &&
            noexcept(other = std::move(*this))
        ) {
            if (this == &other) {
                return;
            }

            List<T, N, Equal, Less> temp = std::move(*this);
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

        /* Equivalent to `size()`, but returns a signed result. */
        [[nodiscard]] constexpr index_type ssize() const noexcept { return index_type(size()); }

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
            return sizeof(List<T, N, Equal, Less>) + capacity() * sizeof(T);
        }

        /* Estimates the maximum amount of memory that the list could consume.  This is
        obtained by extrapolating from `N`, which defaults to a maximum capacity of a few
        MiB.  Note that this indicates potential memory consumption, and not the actual
        amount of memory currently being used. */
        [[nodiscard]] static constexpr size_type max_nbytes() noexcept {
            return sizeof(List<T, N, Equal, Less>) + N * sizeof(T);
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

        [[nodiscard]] constexpr iterator begin()
            noexcept(noexcept(iterator(data())))
        {
            return {data()};
        }

        [[nodiscard]] constexpr const_iterator begin() const
            noexcept(noexcept(const_iterator(data())))
        {
            return {data()};
        }

        [[nodiscard]] constexpr const_iterator cbegin() const
            noexcept(noexcept(const_iterator(data())))
        {
            return {data()};
        }

        [[nodiscard]] constexpr iterator end()
            noexcept(noexcept(iterator(data() + size())))
        {
            return {data() + size()};
        }

        [[nodiscard]] constexpr const_iterator end() const
            noexcept(noexcept(const_iterator(data() + size())))
        {
            return {data() + size()};
        }

        [[nodiscard]] constexpr const_iterator cend() const
            noexcept(noexcept(const_iterator(data() + size())))
        {
            return {data() + size()};
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

        /* Return a bounds-checked iterator to a specific index of the list.  The index may
        be negative, in which case Python-style wraparound is applied (e.g. `-1` refers to
        the last element, `-2` to the second to last, etc.).  An `IndexError` will be
        thrown if the index is out of range after normalization.  The resulting iterator
        can be assigned to in order to simulate indexed assignment into the list, and can
        be implicitly converted to arbitrary types as if it were a typical reference. */
        [[nodiscard]] constexpr iterator operator[](index_type i) {
            return {data() + impl::normalize_index(size(), i)};
        }

        /* Return a bounds-checked iterator to a specific index of the list.  The index may
        be negative, in which case Python-style wraparound is applied (e.g. `-1` refers to
        the last element, `-2` to the second to last, etc.).  An `IndexError` will be
        thrown if the index is out of range after normalization.  The resulting iterator
        can be assigned to in order to simulate indexed assignment into the list, and can
        be implicitly converted to arbitrary types as if it were a typical reference. */
        [[nodiscard]] constexpr const_iterator operator[](index_type i) const {
            return {data() + impl::normalize_index(size(), i)};
        }

        /* Return a view over a particular slice of the list by providing Python-style
        start, stop, and step arguments as a braced initializer.  Each argument can be a
        possibly negative integer, in which case the same wraparound will be applied as for
        scalar indexing, and out of bounds indices will be truncated to the nearest end.
        They can also be omitted either implicitly or explicitly via `std::nullopt`, which
        is replaced with the proper default based on the sign of the step size (itself
        defaulting to 1).

        The returned slice is a valid, explicitly sized range, which can be iterated over
        to yield the underlying elements or implicitly converted (as an rvalue) to any
        other container type via `std::ranges::to()`.  If the slice is mutable, then an
        equivalently-sized range can also be assigned to the slice, which will overwrite
        the underlying list elements in-place.  Note that such assignments cannot change
        the overall size of the list, which outlaws some slice-based insertions that
        would be valid in standard Python.  Such insertions are counterintuitive, and have
        been abstracted into a separate `list.merge(it, values...)` method for clarity. */
        [[nodiscard]] constexpr slice operator[](bertrand::slice s) {
            return {data(), s.normalize(ssize())};
        }

        /* Return a view over a particular slice of the list by providing Python-style
        start, stop, and step arguments as a braced initializer.  Each argument can be a
        possibly negative integer, in which case the same wraparound will be applied as for
        scalar indexing, and out of bounds indices will be truncated to the nearest end.
        They can also be omitted either implicitly or explicitly via `std::nullopt`, which
        is replaced with the proper default based on the sign of the step size (itself
        defaulting to 1).

        The returned slice is a valid, explicitly sized range, which can be iterated over
        to yield the underlying elements or implicitly converted (as an rvalue) to any
        other container type via `std::ranges::to()`.  If the slice is mutable, then an
        equivalently-sized range can also be assigned to the slice, which will overwrite
        the underlying list elements in-place.  Note that such assignments cannot change
        the overall size of the list, which outlaws some slice-based insertions that
        would be valid in standard Python.  Such insertions are counterintuitive, and have
        been abstracted into a separate `list.merge(it, values...)` method for clarity. */
        [[nodiscard]] constexpr const_slice operator[](bertrand::slice s) const {
            return {data(), s.normalize(ssize())};
        }

        /* Resize the allocator to store at least `n` elements.  Returns the number of
        additional elements that were allocated. */
        [[maybe_unused]] constexpr size_type reserve(size_type n) noexcept(
            !DEBUG && noexcept(m_alloc.grow(n))
        ) {
            if (n > capacity()) {
                if constexpr (DEBUG) {
                    if (n > max_capacity()) {
                        throw ValueError(
                            "cannot reserve more than " +
                            static_str<>::from_int<max_capacity()> + " elements"
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
        capacity.  If `n` is less than the current size, this method will only shrink to
        that size and no lower, so as not to evict any elements. */
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

        /* Remove all elements from the list, resetting the size to zero, but leaving the
        capacity unchanged.  Returns the number of items that were removed. */
        [[maybe_unused]] constexpr size_type clear() noexcept(noexcept(m_alloc.clear())) {
            size_type result = size();
            m_alloc.clear();
            return result;
        }

        /* Efficiently remove the last item in the list.  Throws an `IndexError` if the
        list is empty. */
        constexpr void remove() noexcept(
            !DEBUG &&
            noexcept(m_alloc.destroy(data()))
        ) {
            if constexpr (DEBUG) {
                if (empty()) {
                    throw IndexError("cannot remove from empty list");
                }
            }
            m_alloc.destroy(data() + size() - 1);
        }

        /* Remove the item referenced by the given iterator.  All subsequent elements
        will be shifted one index down the list.  Throws an `IndexError` if the iterator
        is out of bounds. */
        constexpr void remove(const iterator& it) {
            if constexpr (DEBUG) {
                if (it < begin() || it >= end()) {
                    throw IndexError("iterator out of bounds");
                }
            }
            size_t idx = it - begin();
            m_alloc.destroy(data() + idx);
            for (ssize_t i = idx; i < size(); ++i) {
                m_alloc.construct(
                    m_alloc.data() + i,
                    std::move(m_alloc.data()[i + 1])
                );
                m_alloc.destroy(m_alloc.data() + i + 1);
            }
        }

        /* Remove the contents of a slice from the list.  Does nothing if the slice is
        empty. */
        constexpr void remove(const slice& slice) {
            if (slice.empty()) {
                return;
            }

            /// NOTE: It is possible to do this in strict O(n) time by destroying and
            /// shifting items within the same loop.  However, since the shifts are
            /// asymmetric with respect to traversal direction, this requires some
            /// normalization, such that we always traverse the slice from left to
            /// right, enabling efficient shifting of elements in the opposite
            /// direction.

            ssize_t index;
            ssize_t stop;
            ssize_t step;
            ssize_t size = this->size();
            if (slice.step() > 0) {
                step = slice.step();
                index = slice.start();
                stop = slice.stop();
            } else {
                step = -slice.step();
                index = slice.stop() + (slice.start() - slice.stop()) % step;
                stop = slice.start() + 1;
            }

            // handle the first deleted element specially, then leap frog so as
            // not to walk off the end of the list
            m_alloc.destroy(data() + index);
            ssize_t offset = 1;
            ssize_t next = index + step;
            while (next < stop) {
                // left shift next `step` elements
                while (++index < next) {
                    m_alloc.construct(
                        data() + index - offset,
                        std::move(data()[index])
                    );
                    m_alloc.destroy(data() + index);
                }

                // delete the next element
                m_alloc.destroy(data() + index);
                ++offset;
                next += step;
            }

            // shift remaining elements to fill in gaps
            while (++index < size) {
                m_alloc.construct(
                    data() + index - offset,
                    std::move(data()[index])
                );
                m_alloc.destroy(data() + index);
            }
        }

        /* Efficiently remove the last item in the list and return its value.  Throws
        an `IndexError` if the list is empty. */
        [[nodiscard]] constexpr meta::unqualify<value_type> pop() noexcept(
            !DEBUG &&
            noexcept(meta::unqualify<value_type>{std::move(back())}) &&
            noexcept(m_alloc.destroy(data()))
        ) {
            if constexpr (DEBUG) {
                if (empty()) {
                    throw IndexError("cannot pop from empty list");
                }
            }
            meta::unqualify<value_type> item {std::move(back())};
            m_alloc.destroy(data() + size() - 1);
            return item;
        }

        /* Remove the item pointed to by the given iterator and return its value.  All
        subsequent elements will be shifted one index down the list.  Throws an
        `IndexError` if the iterator is out of bounds. */
        [[nodiscard]] constexpr meta::unqualify<value_type> pop(const iterator& it) {
            if constexpr (DEBUG) {
                if (it < begin() || it >= end()) {
                    throw IndexError("iterator out of bounds");
                }
            }
            meta::unqualify<value_type> item {std::move(*it)};
            size_t idx = it - begin();
            m_alloc.destroy(data() + idx);
            for (ssize_t i = idx; i < size(); ++i) {
                m_alloc.construct(
                    m_alloc.data() + i,
                    std::move(m_alloc.data()[i + 1])
                );
                m_alloc.destroy(m_alloc.data() + i + 1);
            }
            return item;
        }

        /* Remove the contents of a slice from the list and extract them into a
        separate list.  The result will be empty if the slice is empty. */
        [[nodiscard]] constexpr List<T, N, Equal, Less> pop(const slice& slice) {
            if (slice.empty()) {
                return {};
            }

            List<T, N, Equal, Less> result;
            result.reserve(slice.size());

            /// NOTE: It is possible to do this in strict O(n) time by destroying and
            /// shifting items within the same loop.  However, since the shifts are
            /// asymmetric with respect to traversal direction, this requires some
            /// normalization, such that we always traverse the slice from left to
            /// right, enabling efficient shifting of elements in the opposite
            /// direction.

            ssize_t index;
            ssize_t stop;
            ssize_t step;
            ssize_t size = this->size();
            if (slice.step() > 0) {
                step = slice.step();
                index = slice.start();
                stop = slice.stop();
            } else {
                step = -slice.step();
                index = slice.stop() + (slice.start() - slice.stop()) % step;
                stop = slice.start() + 1;
            }

            // handle the first deleted element specially, then leap frog so as
            // not to walk off the end of the list
            result.m_alloc.construct(result.data(), std::move(data()[index]));
            m_alloc.destroy(data() + index);
            ssize_t offset = 1;
            ssize_t next = index + step;
            while (next < stop) {
                // left shift next `step` elements
                while (++index < next) {
                    m_alloc.construct(
                        data() + index - offset,
                        std::move(data()[index])
                    );
                    m_alloc.destroy(data() + index);
                }

                // delete the next element
                result.m_alloc.construct(result.data() + offset, std::move(data()[index]));
                m_alloc.destroy(data() + index);
                ++offset;
                next += step;
            }

            // shift remaining elements to fill in gaps
            while (++index < size) {
                m_alloc.construct(
                    data() + index - offset,
                    std::move(data()[index])
                );
                m_alloc.destroy(data() + index);
            }

            return result;
        }

        /* Apply a lexicographic comparison between this list and another iterable whose
        elements are 3-way comparable. */
        template <meta::iterable Range>
            requires (requires(const List<T, N, Equal, Less>& self, Range range) {
                std::lexicographical_compare_three_way(
                    self.begin(),
                    self.end(),
                    std::ranges::begin(range),
                    std::ranges::end(range)
                );
            })
        [[nodiscard]] constexpr auto operator<=>(Range&& range) const noexcept(
            noexcept(std::lexicographical_compare_three_way(
                begin(),
                end(),
                std::ranges::begin(range),
                std::ranges::end(range)
            ))
        ) {
            return std::lexicographical_compare_three_way(
                begin(),
                end(),
                std::ranges::begin(range),
                std::ranges::end(range)
            );
        }

        /* Special case for lexicographic equality comparisons which attempts to
        check the size before proceeding to a lexicographic loop. */
        template <meta::iterable Range>
            requires (requires(const List<T, N, Equal, Less>& self, Range range) {
                { std::lexicographical_compare_three_way(
                    self.begin(),
                    self.end(),
                    std::ranges::begin(range),
                    std::ranges::end(range)
                ) == 0 } -> meta::convertible_to<bool>;
            })
        [[nodiscard]] constexpr bool operator==(Range&& range) const noexcept(
            (!meta::has_size<Range> || meta::nothrow::has_size<Range>) &&
            noexcept(std::lexicographical_compare_three_way(
                begin(),
                end(),
                std::ranges::begin(range),
                std::ranges::end(range)
            ) == 0)
        ) {
            if constexpr (meta::has_size<Range>) {
                if (size() != std::ranges::size(range)) {
                    return false;
                }
            }
            return std::lexicographical_compare_three_way(
                begin(),
                end(),
                std::ranges::begin(range),
                std::ranges::end(range)
            ) == 0;
        }
    };

}


namespace meta {

    template <typename T>
    concept List = inherits<T, impl::list_tag>;

    namespace detail {

        template <typename Less, typename T, size_t N, typename Equal, typename L>
        struct sorted<Less, bertrand::List<T, N, Equal, L>> {
            using type = bertrand::List<T, N, Equal, Less>;
        };

        template <typename T, size_t N, typename Equal, typename Less>
        constexpr bool enable_unpack_operator<bertrand::List<T, N, Equal, Less>> = true;

        template <typename T, size_t N, typename Equal, typename Less>
        constexpr bool enable_comprehension_operator<bertrand::List<T, N, Equal, Less>> = true;

    }

}


template <meta::not_void T, size_t N, meta::unqualified Equal, meta::unqualified Less>
    requires (!meta::reference<T> && (
        meta::is_void<Equal> || (
            meta::default_constructible<Equal> &&
            meta::invoke_returns<
                bool,
                meta::as_lvalue<Equal>,
                meta::as_lvalue<meta::as_const<T>>,
                meta::as_lvalue<meta::as_const<T>>
            >
        )
    ) && (
        meta::is_void<Less> || (
            meta::default_constructible<Less> &&
            meta::invoke_returns<
                bool,
                meta::as_lvalue<Less>,
                meta::as_lvalue<meta::as_const<T>>,
                meta::as_lvalue<meta::as_const<T>>
            >
        )
    ))
struct List : impl::list_base<T, N, Equal, Less> {
private:
    using base = impl::list_base<T, N, Equal, Less>;
    using base::lvalue;
    using base::m_alloc;

    template <typename V>
    constexpr auto binary_search(Less& compare, const V& value) const
        noexcept(noexcept(compare(*data(), value)))
    {
        size_type low = 0;
        size_type high = size();
        while (low < high) {
            size_type mid = (low + high) / 2;
            if (compare(data()[mid], value)) {
                low = mid + 1;
            } else {
                high = mid;
            }
        }
        return low;
    }

public:
    using equal_type = base::equal_type;
    using less_type = base::less_type;
    using size_type = base::size_type;
    using index_type = base::index_type;
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

    using base::base;
    using base::size;
    using base::operator bool;
    using base::empty;
    using base::capacity;
    using base::max_capacity;
    using base::nbytes;
    using base::max_nbytes;
    using base::data;
    using base::front;
    using base::back;
    using base::reserve;
    using base::shrink;
    using base::clear;
    using base::begin;
    using base::end;
    using base::cbegin;
    using base::cend;
    using base::rbegin;
    using base::rend;
    using base::crbegin;
    using base::crend;
    using base::operator[];

    /* Initializer list constructor. */
    [[nodiscard]] constexpr List(std::initializer_list<T> items) {
        update(items.begin(), items.end());
    }

    /* Conversion constructor from an iterable range whose contents are convertible to
    the stored type `T`. */
    template <meta::yields<T> Range>
    [[nodiscard]] constexpr explicit List(Range&& range) {
        update(std::forward<Range>(range));
    }

    /* Conversion constructor from an iterator pair whose contents are convertible to
    the stored type `T`. */
    template <meta::iterator Begin, meta::sentinel_for<Begin> End>
        requires (
            meta::not_const<Begin> &&
            meta::not_const<End> &&
            meta::dereferences_to<meta::as_lvalue<Begin>, value_type>
        )
    [[nodiscard]] constexpr explicit List(Begin&& begin, End&& end) {
        update(std::forward<Begin>(begin), std::forward<End>(end));
    }




    /* Insert an item into a sorted list, maintaining proper order.  A binary search
    will be performed to find the proper insertion point, and all subsequent elements
    will be shifted one index down the list.  If an item compares equal to the inserted
    value, then the final ordering between them is not defined.  All other arguments
    are forwarded to the constructor for `T`. */
    template <typename... Args> requires (meta::constructible_from<T, Args...>)
    [[maybe_unused]] constexpr iterator insert(Args&&... args) noexcept(
        true  // TODO: fix this
    ) {
        /// TODO: construct a temporary item, then binary search for the
        /// insertion point, then move all subsequent elements down one index and
        /// insert the new item at the insertion point
    }


    /* Insert items from an input range into a sorted list, maintaining proper order.
    Returns the number of items that were inserted.  In the event of an error, the list
    will be rolled back to its original state before propagating the error. */
    [[maybe_unused]] constexpr size_type update(std::initializer_list<T> items) {
        return update(items.begin(), items.end());
    }

    /* Insert items from an input range into a sorted list, maintaining proper order.
    Returns the number of items that were inserted.  In the event of an error, the list
    will be rolled back to its original state before propagating the error. */
    template <meta::yields<value_type> Range>
    [[maybe_unused]] constexpr size_type update(Range&& range) {
        /// TODO: basically identical to extend() for the sorted case

        if constexpr (meta::not_void<Less>) {
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
    template <meta::iterator Begin, meta::sentinel_for<Begin> End>
        requires (meta::dereferences_to<meta::as_lvalue<Begin>, value_type>)
    [[maybe_unused]] constexpr size_type update(Begin&& begin, End&& end) {
        /// TODO: basically identical to extend() for the sorted case

        if constexpr (meta::not_void<Less>) {
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


    /// TODO: remove(slice)

    /// TODO: pop(), pop(it), pop(slice)


    /// TODO: use meta::as_lvalue

    /* Binary search a sorted list, returning an iterator to the leftmost element that
    is equal to or greater than the given value.  Returns an end iterator if no such
    element exists. */
    template <typename V>
        requires (meta::invoke_returns<
            bool,
            meta::as_lvalue<Less>,
            meta::as_lvalue<meta::as_const<value_type>>,
            meta::as_lvalue<meta::as_const<V>>
        >)
    [[nodiscard]] constexpr iterator nearest(const V& value) const noexcept(
        noexcept(lvalue(Less{})(*data(), value))
    ) {
        Less compare;
        return {data(), binary_search(compare, value), size()};
    }

    /* Find the first occurrence of a given value within the list, returning an
    iterator to the leftmost element that compares equal to it.  Returns an end
    iterator if no such element exists.  If the list is sorted, then this will be done
    using a binary search.  Otherwise, a linear search will be used. */
    template <typename V>
        requires (
            meta::invoke_returns<
                bool,
                meta::as_lvalue<Less>,
                meta::as_lvalue<meta::as_const<value_type>>,
                meta::as_lvalue<meta::as_const<V>>
            > &&
            meta::invoke_returns<
                bool,
                meta::as_lvalue<Less>,
                meta::as_lvalue<meta::as_const<V>>,
                meta::as_lvalue<meta::as_const<value_type>>
            >
        )
    [[nodiscard]] constexpr iterator find(const V& value) const noexcept(
        noexcept(lvalue(Less{})(*data(), value)) &&
        noexcept(lvalue(Less{})(value, *data()))
    ) {
        Less compare;
        size_type low = binary_search(compare, value);
        if (low == size() || compare(value, data()[low])) {
            return end();
        }
        return {data(), low, size()};
    }

    /* Check whether a given value is contained within the list.  This is equivalent
    to calling `list.find(value) == list.end()`. */
    template <typename V>
        requires (
            meta::invoke_returns<
                bool,
                meta::as_lvalue<Less>,
                meta::as_lvalue<meta::as_const<value_type>>,
                meta::as_lvalue<meta::as_const<V>>
            > &&
            meta::invoke_returns<
                bool,
                meta::as_lvalue<Less>,
                meta::as_lvalue<meta::as_const<V>>,
                meta::as_lvalue<meta::as_const<value_type>>
            >
        )
    [[nodiscard]] constexpr bool contains(const V& value) const
        noexcept(noexcept(find(value)))
    {
        return find(value) != end();
    }

    /* Count the number of occurrences of a particular value within the list.  If the
    list is sorted, then this will be done using a binary search, and then forward
    iterating until the first unequal value is encountered.  Otherwise, a linear search
    will be used. */
    template <typename V>
        requires (
            meta::invoke_returns<
                bool,
                meta::as_lvalue<Less>,
                meta::as_lvalue<meta::as_const<value_type>>,
                meta::as_lvalue<meta::as_const<V>>
            > &&
            meta::invoke_returns<
                bool,
                meta::as_lvalue<Less>,
                meta::as_lvalue<meta::as_const<V>>,
                meta::as_lvalue<meta::as_const<value_type>>
            >
        )
    [[nodiscard]] constexpr size_type count(const V& value) const
        noexcept(noexcept(find(value)))
    {
        size_type count = 0;
        Less compare;
        size_type low = binary_search(value);
        if (low == size() || compare(value, data()[low])) {
            return 0;
        }
        for (size_type i = low; i < size(); ++i) {
            if (compare(data()[i], value)) {
                break;
            } else {
                ++count;
            }
        }
        return count;
    }

    /* Find the index of the first occurence of a given value within the list.  If the
    list is sorted, then this will be done using a binary search.  Otherwise, a linear
    search will be used.  Returns an empty optional if no such element exists. */
    template <typename V>
        requires (
            meta::invoke_returns<
                bool,
                meta::as_lvalue<Less>,
                meta::as_lvalue<meta::as_const<value_type>>,
                meta::as_lvalue<meta::as_const<V>>
            > &&
            meta::invoke_returns<
                bool,
                meta::as_lvalue<Less>,
                meta::as_lvalue<meta::as_const<V>>,
                meta::as_lvalue<meta::as_const<value_type>>
            >
        )
    [[nodiscard]] constexpr std::optional<size_type> index(const V& value) const
        noexcept(noexcept(find(value)))
    {
        auto it = find(value);
        if (it == end()) {
            return std::nullopt;
        }
        return it - begin();
    }

};


template <typename T, size_t N, typename Equal>
struct List<T, N, Equal, void> : impl::list_base<T, N, Equal, void> {
private:
    using base = impl::list_base<T, N, Equal, void>;
    using base::m_alloc;
    using base::shift_right;
    using base::shift_left;

    friend base;

public:
    using equal_type = base::equal_type;
    using less_type = base::less_type;
    using size_type = base::size_type;
    using index_type = base::index_type;
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

    using base::base;
    using base::swap;
    using base::size;
    using base::ssize;
    using base::operator bool;
    using base::empty;
    using base::capacity;
    using base::max_capacity;
    using base::nbytes;
    using base::max_nbytes;
    using base::data;
    using base::front;
    using base::back;
    using base::begin;
    using base::cbegin;
    using base::end;
    using base::cend;
    using base::rbegin;
    using base::crbegin;
    using base::rend;
    using base::crend;
    using base::operator[];
    using base::reserve;
    using base::shrink;
    using base::clear;
    using base::remove;
    using base::pop;

    /* Initializer list constructor. */
    [[nodiscard]] constexpr List(std::initializer_list<value_type> items) noexcept(
        noexcept(extend(items.begin(), items.end()))
    ) {
        extend(items.begin(), items.end());
    }

    /* Conversion constructor from an iterable range whose contents are convertible to
    the stored type `T`. */
    template <meta::yields<value_type> Range>
    [[nodiscard]] constexpr explicit List(Range&& range) noexcept(
        noexcept(extend(std::forward<Range>(range)))
    ) {
        extend(std::forward<Range>(range));
    }

    /* Conversion constructor from an iterator pair whose contents are convertible to
    the stored type `T`. */
    template <meta::iterator Begin, meta::sentinel_for<Begin> End>
        requires (
            meta::not_const<Begin> &&
            meta::not_const<End> &&
            meta::dereferences_to<meta::as_lvalue<Begin>, value_type>
        )
    [[nodiscard]] constexpr explicit List(Begin&& begin, End&& end) noexcept(
        noexcept(extend(std::forward<Begin>(begin), std::forward<End>(end)))
    ) {
        extend(std::forward<Begin>(begin), std::forward<End>(end));
    }

    /* Insert an item at the end of the list.  All arguments are forwarded to the
    constructor for `T`.  Returns an iterator to the appended element, and propagates
    any errors emanating from the constructor without modifying the list. */
    template <typename... Args> requires (meta::constructible_from<value_type, Args...>)
    [[maybe_unused]] constexpr iterator append(Args&&... args) & noexcept(
        !DEBUG &&
        noexcept(m_alloc.grow(std::min(capacity() * 2, max_capacity()))) &&
        noexcept(m_alloc.construct(data(), std::forward<Args>(args)...))
    ) {
        if (size() == capacity()) {
            if constexpr (DEBUG) {
                if (capacity() == max_capacity()) {
                    throw ValueError(
                        "list cannot grow beyond maximum length (" +
                        static_str<>::from_int<max_capacity()> + ")"
                    );
                }
            }
            m_alloc.grow(std::min(capacity() * 2, max_capacity()));
        }
        m_alloc.construct(data() + size(), std::forward<Args>(args)...);
        return begin() + (size() - 1);
    }

    /* Insert an item at an arbitrary location within the list, immediately before the
    given iterator.  All subsequent elements (including the item at the iterator
    itself) will be shifted one index up the list.  If the iterator is out of bounds,
    it will be truncated to the nearest valid position.  All other arguments are
    forwarded to the constructor for `T`, and any errors will be propagated, without
    modifying the state of the list.  Returns an iterator to the inserted element,
    which is always equivalent to the input iterator. */
    template <typename... Args> requires (meta::constructible_from<value_type, Args...>)
    [[maybe_unused]] constexpr iterator insert(const_iterator pos, Args&&... args) & noexcept(
        !DEBUG &&
        noexcept(m_alloc.grow(std::min(capacity() * 2, max_capacity()))) &&
        noexcept(m_alloc.construct(data(), std::forward<Args>(args)...)) &&
        noexcept(m_alloc.destroy(data()))
    ) {
        // truncate iterator to bounds of list
        if (pos < begin()) {
            pos = begin();
        } else if (pos > end()) {
            pos = end();
        }

        // grow if necessary
        if (size() == capacity()) {
            if constexpr (DEBUG) {
                if (capacity() == max_capacity()) {
                    throw ValueError(
                        "list exceeds maximum length (" +
                        static_str<>::from_int<max_capacity()> + ")"
                    );
                }
            }
            m_alloc.grow(std::min(capacity() * 2, max_capacity()));
        }

        // move all subsequent elements to the right one index
        size_t idx = pos - begin();
        shift_right(idx, 1);

        // insert new element at iterator position.  If the constructor fails, then
        // we attempt to shift all subsequent elements back to their original positions
        try {
            m_alloc.construct(data() + idx, std::forward<Args>(args)...);
        } catch (...) {
            shift_left(idx, 1);
            throw;
        }

        return begin() + idx;
    }

    /* Insert items from an input range at the end of the list, returning the number of
    items that were inserted.  In the event of an error, the list will be rolled back
    to its original state before propagating the error. */
    [[maybe_unused]] constexpr void extend(std::initializer_list<value_type> items) & {
        return extend(items.begin(), items.end());
    }

    /* Insert items from an input range at the end of the list, returning the number of
    items that were inserted.  In the event of an error, the list will be rolled back
    to its original state before propagating the error. */
    template <meta::yields<value_type> Range>
    [[maybe_unused]] constexpr void extend(Range&& range) & {
        using RangeRef = meta::as_lvalue<Range>;
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
                        if (capacity() == max_capacity()) {
                            throw ValueError(
                                "list exceeds maximum length (" +
                                static_str<>::from_int<max_capacity()> + ")"
                            );
                        }
                    }
                    m_alloc.grow(std::min(capacity() * 2, max_capacity()));
                }
            }
            try {
                m_alloc.construct(data() + size(), *begin);
            } catch (...) {
                size_type new_size = size();
                for (size_t j = old_size; j < new_size; ++j) {
                    m_alloc.destroy(data() + j);
                }
                throw;
            }
            ++begin;
        }
    }

    /* Insert items from an input range at the end of the list, returning the number of
    items that were inserted.  In the event of an error, the list will be rolled back
    to its original state before propagating the error. */
    template <meta::iterator Begin, meta::sentinel_for<Begin> End>
        requires (meta::dereferences_to<meta::as_lvalue<Begin>, value_type>)
    [[maybe_unused]] constexpr void extend(Begin&& begin, End&& end) & {
        using BeginRef = meta::as_lvalue<Begin>;
        using EndRef = meta::as_lvalue<End>;
        if constexpr (meta::sized_sentinel_for<EndRef, BeginRef>) {
            reserve(end - begin);
        }

        size_type old_size = size();
        while (begin != end) {
            if constexpr (!meta::sized_sentinel_for<EndRef, BeginRef>) {
                if (size() == capacity()) {
                    if constexpr (DEBUG) {
                        if (capacity() == max_capacity()) {
                            throw ValueError(
                                "list exceeds maximum length (" +
                                static_str<>::from_int<max_capacity()> + ")"
                            );
                        }
                    }
                    m_alloc.grow(std::min(capacity() * 2, max_capacity()));
                }
            }
            try {
                m_alloc.construct(data() + size(), *begin);
            } catch (...) {
                size_type new_size = size();
                for (size_t j = old_size; j < new_size; ++j) {
                    m_alloc.destroy(data() + j);
                }
                throw;
            }
            ++begin;
        }
    }

    /* Insert items from an input range at an arbitrary location within the list,
    immediately before the given iterator.  All subsequent elements (including the item
    at the iterator itself) will be shifted one index up the list.  If the iterator is
    out of bounds, it will be truncated to the nearest valid position.  In the event
    of an error, the list will be rolled back to its original state before propagating
    the error.  Returns the number of items that were inserted. */
    [[maybe_unused]] constexpr void splice(
        const_iterator pos,
        std::initializer_list<value_type> items
    ) & {
        return splice(pos, items.begin(), items.end());
    }

    /* Insert items from an input range at an arbitrary location within the list,
    immediately before the given iterator.  All subsequent elements (including the item
    at the iterator itself) will be shifted one index up the list.  If the iterator is
    out of bounds, it will be truncated to the nearest valid position.  In the event
    of an error, the list will be rolled back to its original state before propagating
    the error.  Returns a perfectly-forwarded self-reference to allow chaining. */
    template <meta::yields<value_type> Range>
    [[maybe_unused]] constexpr void splice(const_iterator pos, Range&& range) & {
        // get size of input range (potentially O(n) if the range is not explicitly
        // sized and its iterators do not support constant-time distance)
        auto delta = std::ranges::distance(std::forward<Range>(range));
        if (delta <= 0) {
            return;
        }
        reserve(size() + delta);

        // truncate position to bounds of list
        if (pos < begin()) {
            pos = begin();
        } else if (pos > end()) {
            pos = end();
        }

        // move all subsequent elements to the right by size of range to open gap
        size_t idx = pos - begin();
        shift_right(idx, delta);

        // insert items from range.  If an error occurs, then we remove all items that
        // have been added thus far and shift all subsequent elements back to their
        // original positions
        size_type i = idx;
        auto begin = std::ranges::begin(range);
        auto end = std::ranges::end(range);
        while (begin != end) {
            try {
                m_alloc.construct(data() + i, *begin);
            } catch (...) {
                for (size_type j = idx; j < i; ++j) {
                    m_alloc.destroy(data() + j);
                }
                shift_left(idx, delta);
                throw;
            }
            ++i;
            ++begin;
        }
    }

    /* Insert items from an input range at an arbitrary location within the list,
    immediately before the given iterator.  All subsequent elements (including the item
    at the iterator itself) will be shifted one index up the list.  If the iterator is
    out of bounds, it will be truncated to the nearest valid position.  In the event
    of an error, the list will be rolled back to its original state before propagating
    the error.  Returns a perfectly-forwarded self-reference to allow chaining. */
    template <meta::iterator Begin, meta::sentinel_for<Begin> End>
        requires (meta::dereferences_to<meta::as_lvalue<Begin>, value_type>)
    [[maybe_unused]] constexpr void splice(
        const_iterator pos,
        Begin&& begin,
        End&& end
    ) & {
        // get size of input range (potentially O(n) if begin and end do not support
        // constant-time distance)
        auto delta = std::ranges::distance(begin, end);
        if (delta <= 0) {
            return;
        }
        reserve(size() + delta);

        // truncate position to bounds of list
        if (pos < begin()) {
            pos = begin();
        } else if (pos > end()) {
            pos = end();
        }

        // move all subsequent elements to the right by size of range to open gap
        size_t idx = pos - begin();
        shift_right(idx, delta);

        // insert items from range.  If an error occurs, then we remove all items that
        // have been added thus far and shift all subsequent elements back to their
        // original positions
        size_type i = idx;
        while (begin != end) {
            try {
                m_alloc.construct(data() + i, *begin);
            } catch (...) {
                for (size_type j = idx; j < i; ++j) {
                    m_alloc.destroy(data() + j);
                }
                shift_left(idx, delta);
                throw;
            }
            ++i;
            ++begin;
        }
    }

    /* Find the first occurrence of a given value within the list, returning an
    iterator to the leftmost element that compares equal to it.  Returns an end
    iterator if no such element exists.  If the list is sorted, then this will be done
    using a binary search.  Otherwise, a linear search will be used. */
    template <typename V>
        requires (meta::invoke_returns<
            bool,
            meta::as_lvalue<Equal>,
            meta::as_lvalue<meta::as_const<value_type>>,
            meta::as_lvalue<meta::as_const<V>>
        >)
    [[nodiscard]] constexpr iterator find(const V& value) noexcept(
        noexcept(lvalue(Equal{})(*data(), value))
    ) {
        Equal compare;
        for (size_type i = 0; i < size(); ++i) {
            if (compare(data()[i], value)) {
                return {data(), i};
            }
        }
        return end();
    }

    /* Find the first occurrence of a given value within the list, returning an
    iterator to the leftmost element that compares equal to it.  Returns an end
    iterator if no such element exists.  If the list is sorted, then this will be done
    using a binary search.  Otherwise, a linear search will be used. */
    template <typename V>
        requires (meta::invoke_returns<
            bool,
            meta::as_lvalue<Equal>,
            meta::as_lvalue<meta::as_const<value_type>>,
            meta::as_lvalue<meta::as_const<V>>
        >)
    [[nodiscard]] constexpr const_iterator find(const V& value) const noexcept(
        noexcept(lvalue(Equal{})(*data(), value))
    ) {
        Equal compare;
        for (size_type i = 0; i < size(); ++i) {
            if (compare(data()[i], value)) {
                return {data(), i};
            }
        }
        return end();
    }

    /* Check whether a given value is contained within the list.  This is equivalent
    to calling `list.find(value) == list.end()`. */
    template <typename V>
        requires (meta::invoke_returns<
            bool,
            meta::as_lvalue<Equal>,
            meta::as_lvalue<meta::as_const<value_type>>,
            meta::as_lvalue<meta::as_const<V>>
        >)
    [[nodiscard]] constexpr bool contains(const V& value) const noexcept(
        noexcept(find(value))
    ) {
        return find(value) != end();
    }

    /* Count the number of occurrences of a particular value within the list.  If the
    list is sorted, then this will be done using a binary search, and then forward
    iterating until the first unequal value is encountered.  Otherwise, a linear search
    will be used. */
    template <typename V>
        requires (meta::invoke_returns<
            bool,
            meta::as_lvalue<Equal>,
            meta::as_lvalue<meta::as_const<value_type>>,
            meta::as_lvalue<meta::as_const<V>>
        >)
    [[nodiscard]] constexpr size_type count(const V& value) const noexcept(
        noexcept(find(value))
    ) {
        size_type count = 0;
        Equal compare;
        for (size_type i = 0; i < size(); ++i) {
            if (compare(data()[i], value)) {
                ++count;
            }
        }
        return count;
    }

    /* Find the index of the first occurence of a given value within the list.  If the
    list is sorted, then this will be done using a binary search.  Otherwise, a linear
    search will be used.  Returns an empty optional if no such element exists. */
    template <typename V>
        requires (meta::invoke_returns<
            bool,
            meta::as_lvalue<Equal>,
            meta::as_lvalue<meta::as_const<value_type>>,
            meta::as_lvalue<meta::as_const<V>>
        >)
    [[nodiscard]] constexpr std::optional<size_type> index(const V& value) const noexcept(
        noexcept(find(value))
    ) {
        auto it = find(value);
        if (it == end()) {
            return std::nullopt;
        }
        return it - begin();
    }

    /* Sort the list in-place according to a given comparison function, defaulting to
    a transparent `<` operator. */
    template <typename Less = std::less<>>
        requires (requires(List& list, Less less_than) {
            bertrand::sort(list, std::forward<Less>(less_than));
        })
    constexpr void sort(Less&& less_than = {}) & noexcept(
        noexcept(bertrand::sort(*this, std::forward<Less>(less_than)))
    ) {
        bertrand::sort(*this, std::forward<Less>(less_than));
    }

    /* Concatenate this list with another input iterable to produce a new list
    containing all elements from both lists.  The original list is unchanged. */
    template <meta::yields<value_type> Range>
    [[nodiscard]] constexpr List operator+(Range&& range) const noexcept(
        noexcept(List{*this}) &&
        noexcept(extend(std::forward<Range>(range)))
    ) {
        if constexpr (meta::has_size<Range>) {
            List result;
            result.reserve(size() + std::ranges::size(range));
            result.extend(*this);
            result.extend(std::forward<Range>(range));
            return result;

        } else {
            List copy = *this;
            copy.extend(std::forward<Range>(range));
            return copy;
        }
    }

    /* Concatenate this list with another input iterable in-place.  This is equivalent
    to calling `list.extend()`. */
    template <meta::yields<value_type> Range>
    [[maybe_unused]] constexpr List& operator+=(Range&& range) & noexcept(
        noexcept(extend(std::forward<Range>(range)))
    ) {
        extend(std::forward<Range>(range));
        return *this;
    }

    /* Repeat the contents of this list to produce a new list containing `n` copies of
    the original elements.  The original list is unchanged. */
    [[nodiscard]] constexpr List operator*(index_type n) const noexcept(
        noexcept(List{*this}) &&
        noexcept(extend(std::declval<List>()))
    ) {
        List result;
        if (n > 0) {
            result.reserve(size() * n);
            for (index_type i = 0; i < n; ++i) {
                result.extend(*this);
            }
        }
        return result;
    }

    /* Repeat the contents of this list in-place. */
    [[maybe_unused]] constexpr List& operator*=(index_type n) & noexcept(
        noexcept(extend(begin(), end()))
    ) {
        if (n > 0) {
            auto sentinel = end();
            for (index_type i = 1; i < n; ++i) {
                extend(begin(), sentinel);
            }
        } else {
            clear();
        }
        return *this;
    }
};


template <typename T>
List(std::initializer_list<T>) -> List<meta::remove_reference<T>>;


template <meta::iterable Range>
List(Range&&) -> List<meta::remove_reference<meta::yield_type<Range>>>;


template <meta::iterator Begin, meta::sentinel_for<Begin> End>
    requires (meta::not_const<Begin> && meta::not_const<End>)
List(Begin&&, End&&) -> List<meta::remove_reference<
    meta::dereference_type<meta::as_lvalue<Begin>>
>>;


}  // namespace bertrand


namespace std {

    /// NOTE: specializing `std::tuple_size`/`std::tuple_element` and `std::get` allows
    /// small lists to be destructured using structured bindings.

    template <bertrand::meta::List T>
    struct tuple_size<T> :
        std::integral_constant<size_t, bertrand::meta::unqualify<T>::max_capacity()>
    {};

    template <size_t I, bertrand::meta::List T>
        requires (I < bertrand::meta::unqualify<T>::max_capacity())
    struct tuple_element<I, T> {
        using type = bertrand::meta::qualify<
            typename bertrand::meta::unqualify<T>::value_type,
            T
        >;
    };

    template <size_t I, bertrand::meta::List T>
        requires (I < bertrand::meta::unqualify<T>::max_capacity())
    [[nodiscard]] constexpr tuple_element<I, T>::type get(T&& list) noexcept {
        return std::forward<T>(list).data()[I];
    }

}  // namespace std


namespace bertrand {

    inline void test() {
        List<std::string> x = {"a", "b", "c"};
        // bertrand::sort(x);
        // std::vector<std::string> y = {"a", "b", "c"};
        // bertrand::sort(y.begin(), y.end(), &std::string::size);

        typename List<std::string>::const_iterator abc = x.begin();

        x[2] = "d";
    
        std::string value = x[2];
    
        if (x[2]->empty()) {
            auto item = x.pop(x[2]);
        }
    
        for (auto&& y : x[slice{0, 2}]) {
            std::cout << y << '\n';
        }
    
        x[slice{0, 2}] = {"e", "f"};
    
        std::vector<std::string> vec = x[slice{0, 2}];

        if (x < vec) {
            
        }

        List<int, 2> list {1, 2};
        auto&& [a, b] = list;
    }

}


#endif
