#ifndef BERTRAND_ALLOCATE_H
#define BERTRAND_ALLOCATE_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include <atomic>


namespace bertrand {


namespace impl {

    template <typename U>
    static constexpr size_t capacity_size = sizeof(U);  // never returns 0
    template <meta::is_void U>
    static constexpr size_t capacity_size<U> = 1;

    template <typename U>
    static constexpr size_t capacity_align = alignof(U);  // never returns 0
    template <meta::is_void U>
    static constexpr size_t capacity_align<U> = 1;

    /* A self-aligning helper for defining the capacity of a container using either
    explicit unit literals or raw integers, which are interpreted in units of `T`.
    A capacity of zero may indicate a dynamic size that is specified at runtime.

    Usually, this class is used in a template signature or constructor like so:

        ```
        template <typename T, impl::capacity<T> N = 0>
        struct Foo {
            Foo(impl::capacity<T> n) requires (!N) { ... }
        };

        using Bar = Foo<int, 1024>;  // static capacity
        Foo<double> baz{2048};  // dynamic capacity
        ```

    Instances of this type are also be created by the integer literal suffixes
    `_B`, `_KiB`, `_MiB`, `_GiB`, or `_TiB`, all of which return `capacity<void>`
    (the default), indicating a raw byte count.

    Capacity objects are explicitly convertible to `size_t`, which always returns the
    number of instances of `T` that can be stored, or raw bytes if `T` is `void`.  They
    are also implicitly convertible to any other specialization of `capacity`,
    maintaining proper alignment for the target type (and possibly reducing the byte
    count from this capacity). */
    template <typename T = void>
    struct capacity {
        static constexpr size_t aligned_size = (
            (capacity_size<T> / capacity_align<T>) + ((capacity_size<T> % capacity_align<T>) != 0)
        ) * capacity_align<T>;

        size_t value;

        /* Implicitly convert from `size_t`. */
        [[nodiscard, gnu::always_inline]] constexpr capacity(size_t n = 0) noexcept : value(n) {}

        /* Implicitly convert from any other capacity, adjusting for alignment. */
        template <typename V> requires (!std::same_as<T, V>)
        [[nodiscard, gnu::always_inline]] constexpr capacity(impl::capacity<V> n) noexcept :
            value((n.bytes() / aligned_size))
        {}

        /* The total number of bytes needed to represent the given number of instances
        of type `T`. */
        [[nodiscard, gnu::always_inline]] constexpr size_t bytes() const noexcept {
            return value * aligned_size;
        }

        /* The total number of pages needed to represent the given number of instances
        of type `T`, rounded up to the nearest full page. */
        [[nodiscard, gnu::always_inline]] constexpr size_t pages() const noexcept;

        /* Align the capacity up to the nearest multiple of `alignment`, which must be
        specified in bytes.  The result will be returned as a `capacity<void>` object;
        converting it back to `capacity<T>` will yield the maximum number of instances
        of `T` that fit within that byte count.

        For example, if the current capacity is 1 instance of `int32_t` (4 bytes) and
        the alignment is set to 10 bytes, then the new capacity will be 10 bytes, and
        converting back to `capacity<int32_t>` will yield a value of 2, since 2
        instances of `int32_t` (8 bytes) is the largest multiple that is less than or
        equal to the new capacity. */
        [[nodiscard]] constexpr capacity<> align_up(capacity<> align) noexcept {
            size_t result = value * aligned_size;
            result = (result / align.value) + ((result % align.value) != 0);
            result *= align.value;
            return result;
        }

        /* Align the capacity down to the nearest multiple of `alignment`, which must
        be specified in bytes.  The result will be returned as a `capacity<void>`
        object; converting it back to `capacity<T>` will yield the maximum number of
        of `T` that fit within that byte count.

        For example, if the current capacity is 3 instances of `int32_t` (12 bytes) and
        the alignment is set to 10 bytes, then the new capacity will be 10 bytes,
        and converting back to `capacity<int32_t>` will yield a value of 2, since 2
        instances of `int32_t` (8 bytes) is the largest multiple that is less than or
        equal to the new capacity. */
        [[nodiscard]] constexpr capacity<> align_down(capacity<> align) noexcept {
            size_t result = value * aligned_size;
            result /= align.value;
            result *= align.value;
            return result;
        }

        /* Given a pointer to memory, zero out the bytes corresponding to this
        capacity.  This is equivalent to a `memset()` call, except that during constant
        evaluation, it will compile to an explicit loop to avoid undefined behavior. */
        constexpr void* clear(void* ptr) const noexcept {
            if consteval {
                size_t bytes = this->bytes();
                for (size_t i = 0; i < bytes; ++i) static_cast<std::byte*>(ptr)[i] = std::byte{0};
            } else {
                std::memset(ptr, 0, bytes());
            }
            return ptr;
        }

        [[nodiscard]] constexpr size_t& operator*() noexcept { return value; }
        [[nodiscard]] constexpr const size_t& operator*() const noexcept { return value; }
        [[nodiscard]] constexpr explicit operator size_t() const noexcept { return value; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return value != 0; }

        [[nodiscard]] friend constexpr bool operator==(capacity lhs, capacity rhs) noexcept {
            return lhs.value == rhs.value;
        }

        [[nodiscard]] friend constexpr auto operator<=>(capacity lhs, capacity rhs) noexcept {
            return lhs.value <=> rhs.value;
        }

        constexpr capacity& operator++() noexcept {
            ++value;
            return *this;
        }

        [[nodiscard]] constexpr capacity operator++(int) noexcept {
            return value++;
        }

        [[nodiscard]] friend constexpr capacity operator+(capacity lhs, capacity rhs) noexcept {
            return capacity(lhs.value + rhs.value);
        }

        constexpr capacity& operator+=(capacity other) noexcept {
            value += other.value;
            return *this;
        }

        constexpr capacity& operator--() noexcept {
            --value;
            return *this;
        }

        [[nodiscard]] constexpr capacity operator--(int) noexcept {
            return value--;
        }

        [[nodiscard]] friend constexpr capacity operator-(capacity lhs, capacity rhs) noexcept {
            return capacity(lhs.value - rhs.value);
        }

        constexpr capacity& operator-=(capacity other) noexcept {
            value -= other.value;
            return *this;
        }

        [[nodiscard]] friend constexpr capacity operator*(capacity lhs, capacity rhs) noexcept {
            return capacity(lhs.value * rhs.value);
        }

        constexpr capacity& operator*=(capacity other) noexcept {
            value *= other.value;
            return *this;
        }

        [[nodiscard]] friend constexpr capacity operator/(capacity lhs, capacity rhs) noexcept {
            return capacity(lhs.value / rhs.value);
        }

        constexpr capacity& operator/=(capacity other) noexcept {
            value /= other.value;
            return *this;
        }

        [[nodiscard]] friend constexpr capacity operator%(capacity lhs, capacity rhs) noexcept {
            return capacity(lhs.value % rhs.value);
        }

        constexpr capacity& operator%=(capacity other) noexcept {
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

        [[nodiscard]] constexpr capacity operator&(capacity other) const noexcept {
            return capacity(value & other.value);
        }

        constexpr capacity& operator&=(capacity other) noexcept {
            value &= other.value;
            return *this;
        }

        [[nodiscard]] constexpr capacity operator|(capacity other) const noexcept {
            return capacity(value | other.value);
        }

        constexpr capacity& operator|=(capacity other) noexcept {
            value |= other.value;
            return *this;
        }

        [[nodiscard]] constexpr capacity operator^(capacity other) const noexcept {
            return capacity(value ^ other.value);
        }

        constexpr capacity& operator^=(capacity other) noexcept {
            value ^= other.value;
            return *this;
        }
    };

}


}


namespace std {

    template <typename T>
    struct hash<bertrand::impl::capacity<T>> {
        [[nodiscard]] static constexpr size_t operator()(
            bertrand::impl::capacity<T> value
        ) noexcept {
            return std::hash<size_t>{}(value.value);
        }
    };

    template <typename T, typename Char>
    struct formatter<bertrand::impl::capacity<T>, Char> : public formatter<size_t, Char> {
        constexpr auto format(bertrand::impl::capacity<T> value, auto& ctx) const {
            return formatter<size_t, Char>::format(value.value, ctx);
        }
    };

}


namespace bertrand {


/* User-defined literal returning an integer-like value corresponding to a raw number
of bytes, for use in defining sized containers.  The result is explicitly convertible
to `size_t`. */
[[nodiscard]] constexpr impl::capacity<> operator""_B(unsigned long long size) noexcept {
    return {size};
}


/* User-defined literal returning an integer-like value corresponding to a raw number
of kilobytes, for use in defining sized containers.  The result is explicitly
convertible to `size_t`. */
[[nodiscard]] constexpr impl::capacity<> operator""_KiB(unsigned long long size) noexcept {
    return {size << 10};
}


/* User-defined literal returning an integer-like value corresponding to a raw number
of megabytes, for use in defining sized containers.  The result is explicitly
convertible to `size_t`. */
[[nodiscard]] constexpr impl::capacity<> operator""_MiB(unsigned long long size) noexcept {
    return {size << 20};
}


/* User-defined literal returning an integer-like value corresponding to a raw number
of gigabytes, for use in defining sized containers.  The result is explicitly
convertible to `size_t`. */
[[nodiscard]] constexpr impl::capacity<> operator""_GiB(unsigned long long size) noexcept {
    return {size << 30};
}


/* User-defined literal returning an integer-like value corresponding to a raw number
of terabytes, for use in defining sized containers.  The result is explicitly
convertible to `size_t`. */
[[nodiscard]] constexpr impl::capacity<> operator""_TiB(unsigned long long size) noexcept {
    return {size << 40};
}


/* The system's page size in bytes.  This must be a nonzero power of two, which is
controlled by the `BERTRAND_PAGE_SIZE` compilation flag.  It will be checked for
correctness at program startup by querying the OS for the actual page size, which can
be up to 1 GiB.  If not specified, it defaults to 4 KiB, which is the default for most
systems. */
#ifdef BERTRAND_PAGE_SIZE
    constexpr impl::capacity<> PAGE_SIZE = BERTRAND_PAGE_SIZE;
#else
    constexpr impl::capacity<> PAGE_SIZE = 4_KiB;
#endif
static_assert(
    std::popcount(PAGE_SIZE.value) == 1,
    "page size must be a nonzero power of two"
);
static_assert(
    PAGE_SIZE <= 1_GiB,
    "Bertrand only supports page sizes up to 1 GiB"
);


/* The position of the active bit in `PAGE_SIZE`.  Left-shifting 1 by this amount
recovers the page size in bytes. */
constexpr uint8_t PAGE_SHIFT = std::countr_zero(PAGE_SIZE.value);


/* A bitmask with 1s for each bit below `PAGE_SHIFT` and 0s elsewhere. */
constexpr size_t PAGE_MASK = PAGE_SIZE.value - 1;


/* The amount of virtual memory allocated per thread.  An allocator with this amount of
memory will be initialized using thread-local storage on first use, and deallocated on
thread shutdown.  This must be either zero (which disables virtual memory entirely) or
a power of two, which is controlled by the `BERTRAND_VMEM_PER_THREAD` compilation
flag. */
#ifdef BERTRAND_VMEM_PER_THREAD
    constexpr impl::capacity<> VMEM_PER_THREAD = BERTRAND_VMEM_PER_THREAD;
#else
    constexpr impl::capacity<> VMEM_PER_THREAD = 8_GiB;
#endif
static_assert(
    std::popcount(VMEM_PER_THREAD.value) <= 1,
    "`VMEM_PER_THREAD` must be a power of two"
);
static_assert(
    VMEM_PER_THREAD % PAGE_SIZE == 0,
    "`VMEM_PER_THREAD` must be a multiple of the page size"
);
static_assert(
    VMEM_PER_THREAD / PAGE_SIZE <= std::numeric_limits<uint32_t>::max(),
    "`VMEM_PER_THREAD` is too large to fully index using 32-bit integers"
);


/* Bertrand's thread-local virtual allocators use batched decommit calls in order to
reduce kernel transition overhead when freeing memory.  The size of each batch is
controlled by the `BERTRAND_VMEM_COALESCE_AFTER` compilation flag, which must be
a multiple of `PAGE_SIZE` specifying the number of bytes that must be pending decommit
before a batch is triggered.  If not specified, it defaults to 16 MiB. */
#ifdef BERTRAND_VMEM_COALESCE_AFTER
    constexpr impl::capacity<> VMEM_COALESCE_AFTER = BERTRAND_VMEM_COALESCE_AFTER;
#else
    constexpr impl::capacity<> VMEM_COALESCE_AFTER = 16_MiB;
#endif
static_assert(
    VMEM_COALESCE_AFTER % PAGE_SIZE == 0,
    "`COALESCE_AFTER` must be a multiple of the page size"
);


/* When issuing batched decommit calls, Bertrand's thread-local virtual allocators must
sort the list of pending decommits in order to coalesce adjacent pages into single
decommit requests.  The general sorting algorithm is an optimized radix sort, but for
small batches, insertion sort may be faster due to lower constant overhead.  The
`BERTRAND_VMEM_INSERTION_THRESHOLD` compilation flag controls the maximum batch size
(in allocations) for which this optimization will be applied.  If not specified, it
defaults to 64 allocations. */
#ifdef BERTRAND_VMEM_INSERTION_THRESHOLD
    constexpr size_t VMEM_INSERTION_THRESHOLD = BERTRAND_VMEM_INSERTION_THRESHOLD;
#else
    constexpr size_t VMEM_INSERTION_THRESHOLD = 64;
#endif


/* Bertrand's thread-local virtual allocators include a comprehensive set of debug
assertions to ensure correctness during testing, but due to their high performance
impact, they will be disabled unless `BERTRAND_VMEM_DEBUG` is defined at compile time.
Since the allocator is internal to Bertrand, these assertions will only be enabled when
testing Bertrand itself. */
#ifdef BERTRAND_VMEM_DEBUG
    constexpr bool VMEM_DEBUG = true;
#else
    constexpr bool VMEM_DEBUG = false;
#endif


/* A contiguous region of reserved addresses holding uninitialized instances of type
`T`, which will be freed when the `Space` object is destroyed.  If `T` is `void`, then
the space will be interpreted as raw bytes, which can be used to store arbitrary data.

Spaces of this form come in 3 flavors, depending on the source of their backing memory:

    1.  Static (stack-based) spaces, where `N` is a compile-time constant indicating
        either the maximum number of instances of `T` that can be stored if it is an
        integer, or the total number of bytes if it uses one of the literal suffixes
        (`_B`, `_KiB`, `_MiB`, `_GiB`, `_TiB`).  These spaces cannot grow, shrink, be
        copied, or moved, and their contents remain uninitialized until explicitly
        constructed by the user.  This is the fastest and most efficient type of space,
        and does not require any dynamic memory allocation whatsoever, but is limited
        to fixed sizes known at compile time, and is therefore less flexible than the
        other options.  These are typically used for in-place vector types as well as
        buffers for string or integer literals, where the size can be detected via
        CTAD and is guaranteed not to grow beyond a maximum limit.
    2.  Dynamic (heap-based) spaces, where `N` is zero (the default), and the size is
        supplied as a constructor argument instead.  These spaces allocate memory using
        `std::allocator<T>`, which is constexpr-capable, and equivalent to traditional
        `new`/`delete` calls under the hood.  These spaces can grow and shrink as
        needed, and may be trivially moved, but not copied.  They suffer from all the
        usual downsides of heap allocation, including fragmentation, relocation of
        existing elements, contention between threads, and unpredictable performance.
        These are typically used as an alternative to (3) below when either virtual
        memory is disabled or the space is constructed at compile time, where heap
        allocation is the only available option.
    3.  Virtual (page-based) spaces, where `N` is again zero, and the size is supplied
        as a constructor argument similar to (2) above.  This type of space will be
        automatically chosen instead of (2) as long as virtual memory is enabled and
        the space is constructed at runtime.  The backing memory will be allocated from
        Bertrand's thread-local virtual address space, which is decoupled from physical
        memory, and supports features like overcommitment, where large regions can be
        reserved without immediately consuming physical RAM.  Instead, physical pages
        will be committed into the virtual address space on first access, which
        generates a page fault that will be handled transparently by the OS.  Upon
        deallocation, the physical pages will be decommitted and returned to the OS
        using a batched syscall, which amortizes its cost.  If the requested size falls
        below a handful of pages, then the space will be obtained from a slab allocator
        within the thread-local pool instead, which reduces both wastage and TLB
        pressure for small allocations.  Such spaces can also be grown, shrunk, or
        moved (but not copied), and are much more likely to avoid relocation compared
        to (2) above, since fragmentation in the virtual address space does not
        necessarily correlate to fragmentation of physical memory, unlike traditional
        heap allocators.  This yields consistent growth performance for dynamic data
        structures, although relocations may still occur during the transition between
        slab size classes or at high load factors.  Additionally, since each address
        space is thread-local, allocations and deallocations are lock-free and
        uncontended, even when deallocating from other threads, which can be done via
        remote free requests that will be processed later by the owning thread without
        blocking.

In all 3 cases, the reserved memory will be deallocated when the space is destroyed,
according to RAII principles.  Note that no destructors will be invoked for any
constructed elements - it is the user's responsibility to manage the lifetime of the
objects stored within the space, and ensure they are properly destroyed before the
space itself.

This class is generally used as a helper to implement higher-level data structures like
`List`, `Str`, and various type-erasure mechanisms, where uninitialized, possibly
dynamic storage is required.  Users may instantiate it directly in order to implement
arena allocators or custom data structures of their own, as long as they properly
manage the construction and destruction of each element.  Note that physical memory
will never leak due to RAII-based cleanup, but logical leaks may still occur if
destructors are used to manage external resources like file handles or network
connections. */
template <meta::unqualified T, impl::capacity<T> N = 0>
struct Space {
    using type = T;

    /* Identifies static (stack-based) vs dynamic (heap or virtual memory-based)
    spaces.  If true, then the space may be grown, shrunk, or moved, but not copied. */
    [[nodiscard]] static constexpr bool dynamic() noexcept { return false; }

    /* Differentiates heap-based and virtual memory-based spaces.  These have identical
    interfaces, but may exhibit different performance characteristics subject to this
    class's documentation. */
    [[nodiscard]] constexpr bool heap() const noexcept { return false; }

    /* Detect whether a virtual memory-based space is backed by a slab allocator or
    page allocations.  Slab allocations are used for small sizes to reduce memory
    overhead and TLB pressure, but cannot be arbitrarily grown or shrunk in-place like
    page allocations can.  Normally, this is not something the user needs to be
    concerned about, but it may be useful for debugging or performance analysis.  Note
    that it is possible to avoid slab allocations entirely by providing an alignment
    requirement greater than a single pointer during construction. */
    [[nodiscard]] constexpr bool slab() const noexcept { return false; }

    /* Returns true if the space owns at least one element. */
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return true; }

    /* Returns true if the space is empty, meaning it does not own any memory. */
    [[nodiscard]] static constexpr bool empty() noexcept { return false; }

    /* Returns the number of elements in the space as an unsigned integer. */
    [[nodiscard]] static constexpr size_t size() noexcept { return N.value; }

    /* Returns the number of elements in the space as a signed integer. */
    [[nodiscard]] static constexpr ssize_t ssize() noexcept { return ssize_t(N.value); }

    /* Returns the space's current capacity in bytes as an `impl::capacity<void>`
    object.  Such capacities can be explicitly converted to `size_t`, dereferenced, or
    accessed via `.value` or `.bytes()`, all of which do the same thing.  Converting
    the result to another non-void capacity will maintain proper alignment for the
    target type, changing units but not the overall memory footprint. */
    [[nodiscard]] static constexpr impl::capacity<> capacity() noexcept { return N; }

private:
    union storage {
        T data[size()];
        /// TODO: once P3074 becomes available, these constructors/destructors can be
        /// safely deleted, allowing elements to be constructed and destroyed at
        /// compile time.
        constexpr storage() noexcept requires (meta::trivially_constructible<T>) = default;
        constexpr storage() noexcept requires (!meta::trivially_constructible<T>) {}
        constexpr ~storage() noexcept requires (meta::trivially_destructible<T>) = default;
        constexpr ~storage() noexcept requires (!meta::trivially_destructible<T>) {};
    };

public:
    storage m_storage;

    /* Default-constructing a static space reserves stack space, but does not
    initialize any of the elements. */
    [[nodiscard]] constexpr Space() noexcept = default;

    /* Static spaces cannot be copied or moved, since they are not aware of which
    elements are constructed and which are not. */
    constexpr Space(const Space&) = delete;
    constexpr Space(Space&&) = delete;
    constexpr Space& operator=(const Space&) = delete;
    constexpr Space& operator=(Space&&) = delete;

    /* Spaces can be used as pointers similar to C-style arrays. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) noexcept {
        return std::forward<Self>(self).front();
    }
    [[nodiscard]] constexpr T* operator->() noexcept { return data(); }
    [[nodiscard]] constexpr const T* operator->() const noexcept { return data(); }

    /* Retrieve a pointer to a specific element identified by index or memory offset,
    defaulting to the first value in the space.  Similar to `construct()` and
    `destroy()`, an alternate type `V` may be specified as a template parameter, in
    which case the returned pointer will be reinterpreted as that type.  Note that the
    returned pointer will always be properly aligned for type `T`, and it is up to the
    user to ensure that neither this nor the reinterpretation leads to undefined
    behavior. */
    template <meta::unqualified V = type>
    [[nodiscard]] constexpr type* data(impl::capacity<type> i = 0) noexcept {
        if constexpr (meta::explicitly_convertible_to<type*, V*>) {
            return static_cast<type*>(m_storage.data + i.value);
        } else {
            return reinterpret_cast<type*>(m_storage.data + i.value);
        }
    }
    template <meta::unqualified V = type>
    [[nodiscard]] constexpr const type* data(impl::capacity<type> i = 0) const noexcept {
        if constexpr (meta::explicitly_convertible_to<type*, V*>) {
            return static_cast<const type*>(m_storage.data + i.value);
        } else {
            return reinterpret_cast<const type*>(m_storage.data + i.value);
        }
    }

    /* Perfectly forward the first element of the space. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) front(this Self&& self) noexcept {
        return (std::forward<Self>(self).m_storage.data[0]);
    }

    /* Perfectly forward the last element of the space. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) back(this Self&& self) noexcept {
        return (std::forward<Self>(self).m_storage.data[ssize() - 1]);
    }

    /* Perfectly forward an element of the space identified by index. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) operator[](
        this Self&& self,
        impl::capacity<type> i
    ) noexcept {
        return (std::forward<Self>(self).m_storage.data[i.value]);
    }

    /* Get a forward iterator over the space. */
    template <typename Self>
    [[nodiscard]] constexpr auto begin(this Self&& self) noexcept {
        if constexpr (meta::lvalue<Self>) {
            return self.data();
        } else {
            return std::move_iterator(self.data());
        }
    }

    /* Get a forward sentinel for the space. */
    template <typename Self>
    [[nodiscard]] constexpr auto end(this Self&& self) noexcept {
        if constexpr (meta::lvalue<Self>) {
            return self.data(N);
        } else {
            return std::move_iterator(self.data(N));
        }
    }

    /* Get a reverse iterator over the space. */
    template <typename Self>
    [[nodiscard]] constexpr auto rbegin(this Self&& self) noexcept {
        return std::make_reverse_iterator(std::forward<Self>(self).end());
    }

    /* Get a reverse sentinel for the space. */
    template <typename Self>
    [[nodiscard]] constexpr auto rend(this Self&& self) noexcept {
        return std::make_reverse_iterator(std::forward<Self>(self).begin());
    }

    /* Construct the element at the specified index of the space using the remaining
    arguments and return a reference to the newly constructed instance.  A different
    type may be provided as a template parameter, in which case the element will be
    reinterpreted as that type before constructing it.  The type may also be provided
    as a template, in which case CTAD will be applied to deduce the actual type to
    construct.  Note that it is up to the user to ensure that this does not lead to
    undefined behavior. */
    template <meta::unqualified V = type, typename... A>
    constexpr decltype(auto) construct(impl::capacity<type> i, A&&... args) &
        noexcept (meta::nothrow::constructible_from<V, A...>)
        requires (meta::constructible_from<V, A...>)
    {
        return (*std::construct_at(data<V>(i), std::forward<A>(args)...));
    }
    template <meta::unqualified V = type, typename... A>
    constexpr decltype(auto) construct(impl::capacity<type> i, A&&... args) &&
        noexcept (meta::nothrow::constructible_from<V, A...>)
        requires (meta::constructible_from<V, A...>)
    {
        return (std::move(*std::construct_at(data<V>(i), std::forward<A>(args)...)));
    }
    template <template <typename...> typename V, typename... A>
    constexpr decltype(auto) construct(impl::capacity<type> i, A&&... args) &
        noexcept (requires{{V{std::forward<A>(args)...}} noexcept;})
        requires (requires{{V{std::forward<A>(args)...}};})
    {
        using to = decltype(V<type>(std::forward<A>(args)...));
        return (*std::construct_at(data<to>(i), std::forward<A>(args)...));
    }
    template <template <typename...> typename V, typename... A>
    constexpr decltype(auto) construct(impl::capacity<type> i, A&&... args) &&
        noexcept (requires{{V{std::forward<A>(args)...}} noexcept;})
        requires (requires{{V{std::forward<A>(args)...}};})
    {
        using to = decltype(V{std::forward<A>(args)...});
        return (std::move(*std::construct_at(data<to>(i), std::forward<A>(args)...)));
    }

    /* Destroy the element at the specified index of the space. */
    template <meta::unqualified V = type>
    constexpr void destroy(impl::capacity<type> i)
        noexcept (meta::nothrow::destructible<V>)
        requires (meta::destructible<V>)
    {
        std::destroy_at(data<V>(i));
    }

    /* Attempt to reserve additional memory for the space without relocating existing
    elements.  The `size` argument indicates the desired minimum capacity of the space
    after growth.  If the current capacity is greater than or equal to the requested
    size, or if the space does not support in-place resizing, or originates from
    another thread, then no action is taken.  These cases can be detected by checking
    the capacity before and after calling this method, and handled by relocating its
    contents to another space if necessary.  Note that no constructors will be called
    for the new elements, so users must ensure that lifetimes are properly managed
    after calling this method. */
    constexpr void reserve(impl::capacity<type> size) noexcept {}

    /* Attempt to partially release memory back to the originating allocator, shrinking
    the space to the requested `size`.  If the current capacity is less than or equal
    to the requested size, or if the space does not support in-place resizing, or
    originates from another thread, then no action is taken.  These cases can be
    detected by checking the capacity before and after calling this method, and handled
    by relocating its contents to another space if necessary.  Note that no destructors
    will be called for the truncated elements, so the user must ensure that lifetimes
    are properly managed before calling this method. */
    constexpr void truncate(impl::capacity<type> size) noexcept {}
};


namespace impl {

    /* Verify that the system's page size matches the configured `PAGE_SIZE` at
    startup. */
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

    template <typename T>
    [[nodiscard, gnu::always_inline]] constexpr size_t capacity<T>::pages() const noexcept {
        size_t bytes = this->bytes();
        return (bytes >> PAGE_SHIFT) + ((bytes & PAGE_MASK) != 0);
    }

    /* Reserve a range of virtual addresses with the given number of bytes, aligned to
    the specified alignment (which must be a power of two).  Returns a void pointer to
    the start of the address range, where all bits that are less significant than the
    alignment bit are set to zero.  A MemoryError may be thrown if the reservation
    fails, and the result will never be null.

    On unix systems, this will reserve the address space with full read/write
    privileges, and physical pages will be mapped into the region by the OS when they
    are first accessed.  On windows systems, the address space is reserved without any
    privileges, and memory must be explicitly committed before use. */
    [[nodiscard]] inline void* map_address_space(
        capacity<> bytes,
        capacity<> alignment = PAGE_SIZE
    ) {
        if constexpr (VMEM_DEBUG) {
            if (bytes.value == 0 || (bytes.value & PAGE_MASK) != 0) {
                throw MemoryError("bytes must be a nonzero multiple of the page size");
            }
            if (std::popcount(alignment.value) != 1) {
                throw MemoryError("alignment must be a power of two");
            }
        }

        #if WINDOWS
            // Windows systems have a minimum allocation granularity that must be
            // respected to properly align allocations
            SYSTEM_INFO si {};
            GetSystemInfo(&si);
            size_t granularity = si.dwAllocationGranularity;
            if constexpr (VMEM_DEBUG) {
                if (alignment.value > granularity && alignment.value % granularity != 0) {
                    throw MemoryError(std::format(
                        "alignment must be either less than or a multiple of the "
                        "Windows allocation granularity ({} bytes)",
                        granularity
                    ));
                }
            }

            // fast path for alignments below allocation granularity
            if (alignment.value <= granularity) {
                void* ptr = VirtualAlloc(
                    nullptr,
                    bytes.value,
                    MEM_RESERVE,  // reserve only (does not charge against commit limit)
                    PAGE_NOACCESS
                );
                if (ptr == nullptr) {
                    throw MemoryError(std::format(
                        "failed to map virtual address space ({:.3f} MiB) -> {}",
                        double(bytes.value) / double((1_MiB).value),
                        system_err_msg()
                    ));
                }
                return ptr;
            }

            // try VirtualAlloc2 with explicit alignment requirements (Windows 10+).
            // If unavailable, fall back to the probe/retry method below
            using VirtualAlloc2_t = PVOID (WINAPI*)(
                HANDLE,
                PVOID,
                SIZE_T,
                ULONG,
                ULONG,
                MEM_EXTENDED_PARAMETER*,
                ULONG
            );
            VirtualAlloc2_t VirtualAlloc2_ptr = nullptr;
            if (HMODULE hKernelBase = GetModuleHandleW(L"KernelBase.dll")) {
                VirtualAlloc2_ptr = reinterpret_cast<VirtualAlloc2_t>(
                    GetProcAddress(hKernelBase, "VirtualAlloc2")
                );
            }
            if (VirtualAlloc2_ptr != nullptr) {
                MEM_ADDRESS_REQUIREMENTS req {};
                req.LowestStartingAddress = nullptr;
                req.HighestEndingAddress = nullptr;
                req.Alignment = alignment.value;

                MEM_EXTENDED_PARAMETER param {};
                param.Type = MemExtendedParameterAddressRequirements;
                param.Pointer = &req;

                void* ptr = VirtualAlloc2_ptr(
                    GetCurrentProcess(),
                    nullptr,
                    bytes.value,
                    MEM_RESERVE,
                    PAGE_NOACCESS,
                    &param,
                    1
                );

                // if VirtualAlloc2 exists but failed, fall through to probe/retry
                if (ptr != nullptr) {
                    if constexpr (VMEM_DEBUG) {
                        if (reinterpret_cast<uintptr_t>(ptr) % alignment.value != 0) {
                            VirtualFree(ptr, 0, MEM_RELEASE);
                            throw MemoryError(
                                "VirtualAlloc2 returned misaligned address despite "
                                "explicit alignment requirements"
                            );
                        }
                    }
                    return ptr;
                }
            }

            // Portable fallback: probe reservation, compute aligned address, then
            // reserve at aligned.  This is race-tolerant (other threads may steal the
            // address), so we retry a bounded number of times before giving up
            constexpr int MAX_ATTEMPTS = 128;

            // Reserve enough space that there is guaranteed to be an aligned subrange
            // of length `bytes`
            size_t probe_size = bytes.value + alignment.value;
            for (int attempt = 0; attempt < MAX_ATTEMPTS; ++attempt) {
                void* probe = VirtualAlloc(nullptr, probe_size, MEM_RESERVE, PAGE_NOACCESS);
                if (probe == nullptr) {
                    throw MemoryError(std::format(
                        "failed to map virtual address space ({:.3f} MiB) -> {}",
                        double(bytes.value) / double((1_MiB).value),
                        system_err_msg()
                    ));
                }

                // align, then release the probe reservation
                void* aligned = reinterpret_cast<void*>(
                    (reinterpret_cast<uintptr_t>(probe) + alignment.value - 1) & ~(alignment.value - 1)
                );
                VirtualFree(probe, 0, MEM_RELEASE);

                // attempt to reserve at the aligned address
                probe = VirtualAlloc(aligned, bytes.value, MEM_RESERVE, PAGE_NOACCESS);
                if (probe == aligned) {
                    return probe;  // success
                }
                if (probe != nullptr) {
                    VirtualFree(probe, 0, MEM_RELEASE);
                }
            }

            // all attempts failed
            throw MemoryError(std::format(
                "failed to map virtual address space ({:.3f} MiB) after {} attempts -> {}",
                double(bytes.value) / double((1_MiB).value),
                MAX_ATTEMPTS,
                system_err_msg()
            ));

        #elif UNIX
            // the result from mmap() is always page-aligned, so any alignment below
            // that is automatically satisfied
            size_t total = bytes.value + alignment.value * (alignment.value > PAGE_SIZE);
            void* ptr = mmap(
                nullptr,
                total,
                PROT_READ | PROT_WRITE,
                #ifdef MAP_NORESERVE
                    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE,
                #else
                    MAP_PRIVATE | MAP_ANONYMOUS,
                #endif
                -1,  // no file descriptor
                0  // no offset
            );
            if (ptr == MAP_FAILED) {
                throw MemoryError(std::format(
                    "failed to map virtual address space ({:.3f} MiB) -> {}",
                    double(bytes.value) / double((1_MiB).value),
                    system_err_msg()
                ));
            }
            if (alignment <= PAGE_SIZE) {
                return ptr;  // trivially aligned
            }

            // align and unmap excess memory
            std::byte* aligned = reinterpret_cast<std::byte*>(
                (reinterpret_cast<uintptr_t>(ptr) + alignment.value - 1) & ~(alignment.value - 1)
            );
            std::ptrdiff_t left = aligned - static_cast<std::byte*>(ptr);
            if (left > 0) munmap(ptr, left);
            std::ptrdiff_t right = (static_cast<std::byte*>(ptr) + total) - (aligned + bytes.value);
            if (right > 0) munmap(aligned + bytes.value, right);
            return aligned;
        #else
            throw MemoryError(
                "Virtual memory is only supported on Windows and Unix systems.  "
                "Please recompile with `VMEM_PER_THREAD` set to zero to disable this "
                "feature."
            );
        #endif
    }

    /* Release a range of virtual addresses starting at the given pointer with the
    specified number of bytes, returning them to the OS.  This is the inverse of
    `map_address_space()`.  A MemoryError may be thrown if the unmapping fails. */
    inline void unmap_address_space(void* ptr, capacity<> bytes) {
        if constexpr (VMEM_DEBUG) {
            if ((reinterpret_cast<uintptr_t>(ptr) & PAGE_MASK) != 0) {
                throw MemoryError("ptr must be aligned to the page size");
            }
            if ((bytes.value & PAGE_MASK) != 0) {
                throw MemoryError("size must be a nonzero multiple of the page size");
            }
        }

        #if WINDOWS
            if (VirtualFree(reinterpret_cast<LPVOID>(ptr), 0, MEM_RELEASE) == 0) {
                throw MemoryError(std::format(
                    "failed to unmap virtual address space starting at address "
                    "{:#x} and ending at address {:#x} ({:.3f} MiB) -> {}",
                    reinterpret_cast<uintptr_t>(ptr),
                    reinterpret_cast<uintptr_t>(static_cast<std::byte*>(ptr) + bytes.value),
                    double(bytes.value) / double((1_MiB).value),
                    system_err_msg()
                ));
            }
        #elif UNIX
            if (munmap(ptr, bytes.value) != 0) {
                throw MemoryError(std::format(
                    "failed to unmap virtual address space starting at address "
                    "{:#x} and ending at address {:#x} ({:.3f} MiB) -> {}",
                    reinterpret_cast<uintptr_t>(ptr),
                    reinterpret_cast<uintptr_t>(static_cast<std::byte*>(ptr) + bytes.value),
                    double(bytes.value) / double((1_MiB).value),
                    system_err_msg()
                ));
            }
        #else
            throw MemoryError(
                "Virtual memory is only supported on Windows and Unix systems.  "
                "Please recompile with `VMEM_PER_THREAD` set to zero to disable this "
                "feature."
            );
        #endif
    }

    /* Commit pages to a portion of a mapped address space starting at `ptr` and
    continuing for `bytes`.  Physical pages will be mapped into the committed region by
    the OS when they are first accessed.  A MemoryError may be thrown if the commit
    fails.

    On unix systems, this function does nothing, since the entire mapped region is
    always committed, and there is no hard commit limit to charge against.  On windows
    systems, this will be called in order to expand the committed region as needed,
    increasing syscall overhead, but minimizing commit charge so as not to interfere
    with other processes on the system. */
    inline void commit_address_space(void* ptr, capacity<> bytes) {
        if constexpr (VMEM_DEBUG) {
            if ((reinterpret_cast<uintptr_t>(ptr) & PAGE_MASK) != 0) {
                throw MemoryError("ptr must be aligned to the page size");
            }
            if ((bytes.value & PAGE_MASK) != 0) {
                throw MemoryError("size must be a nonzero multiple of the page size");
            }
        }

        #if WINDOWS
            if (VirtualAlloc(
                reinterpret_cast<LPVOID>(ptr),
                bytes.value,
                MEM_COMMIT,
                PAGE_READWRITE
            ) == nullptr) {
                throw MemoryError(std::format(
                    "failed to commit {:.3f} MiB of virtual address space "
                    "starting at address {:#x} and ending at address {:#x} -> {}",
                    double(bytes.value) / double((1_MiB).value),
                    reinterpret_cast<uintptr_t>(ptr),
                    reinterpret_cast<uintptr_t>(ptr) + bytes.value,
                    system_err_msg()
                ));
            }
        #elif UNIX
            return;  // no-op
        #else
            throw MemoryError(
                "Virtual memory is only supported on Windows and Unix systems.  "
                "Please recompile with `VMEM_PER_THREAD` set to zero to disable this "
                "feature."
            );
        #endif
    }

    /* Decommit pages from a portion of a mapped address space starting at `ptr` and
    continuing for `bytes`, allowing the OS to reclaim the physical pages associated
    with that region, without affecting the address space itself.  A MemoryError may be
    thrown if the decommit fails.

    On unix systems, this function will be called periodically during the coalesce step
    of the allocator once the total amount of freed memory exceeds a certain threshold.
    On windows systems, this will be called in order to shrink the committed region as
    needed, increasing syscall overhead, but minimizing commit charge so as not to
    interfere with other processes on the system. */
    inline void decommit_address_space(void* ptr, capacity<> bytes) {
        if constexpr (VMEM_DEBUG) {
            if ((reinterpret_cast<uintptr_t>(ptr) & PAGE_MASK) != 0) {
                throw MemoryError("ptr must be aligned to the page size");
            }
            if ((bytes.value & PAGE_MASK) != 0) {
                throw MemoryError("size must be a nonzero multiple of the page size");
            }
        }

        #if WINDOWS
            if (VirtualFree(reinterpret_cast<LPVOID>(ptr), bytes.value, MEM_DECOMMIT) == 0) {
                throw MemoryError(std::format(
                    "failed to decommit {:.3f} MiB of virtual address space "
                    "starting at address {:#x} and ending at address {:#x} -> {}",
                    double(bytes.value) / double((1_MiB).value),
                    reinterpret_cast<uintptr_t>(ptr),
                    reinterpret_cast<uintptr_t>(ptr) + bytes.value,
                    system_err_msg()
                ));
            }
        #elif UNIX
            if (madvise(ptr, bytes.value, MADV_DONTNEED) != 0) {
                throw MemoryError(std::format(
                    "failed to decommit {:.3f} MiB of virtual address space "
                    "starting at address {:#x} and ending at address {:#x} -> {}",
                    double(bytes.value) / double((1_MiB).value),
                    reinterpret_cast<uintptr_t>(ptr),
                    reinterpret_cast<uintptr_t>(ptr) + bytes.value,
                    system_err_msg()
                ));
            }
        #else
            throw MemoryError(
                "Virtual memory is only supported on Windows and Unix systems.  "
                "Please recompile with `VMEM_PER_THREAD` set to zero to disable this "
                "feature."
            );
        #endif
    }

    /// TODO: it may be possible to have the allocator grow over time by mapping
    /// additional regions at the front and end of the existing space, which shouldn't
    /// require relocating existing allocations, but does require the header and
    /// internal data structures to be copied over, and for node indices to remain
    /// stable, which implies that the node storage needs to grow from right to left,
    /// such that the first node that is allocated is the one with the highest index,
    /// rather than the lowest.  Then, upon growth, the new nodes will simply overwrite
    /// the old header/slabs/batch/chunks, etc.

    /// TODO: it should also be possible for `drain_remote()` to aggressively dump to
    /// the batch list, rather than possibly clearing it multiple times during the
    /// remote free and possible subsequent deallocation.

    /// TODO: I may also want to add a debug self-check that asserts each entry in the
    /// heap is also present in the treap and vice versa.

    struct address_space_handle;

    /* Address spaces are split into a base class with all the member variables in
    order to reliably detect its size in later partition calculations. */
    struct address_space_base {
        /* Because we asserted that the address space can be fully indexed using 32-bit
        integers, and we always reserve at least one page as a private partition, we
        can safely reuse the maximum 32-bit unsigned integer as a sentinel value. */
        static constexpr uint32_t NIL = std::numeric_limits<uint32_t>::max();

        /* Due to the windows commit limit, care must be taken not to commit too much
        virtual memory at once (not a problem on unix).  In order to address this, each
        address space will include a ledger tracking chunks of 255 pages at a time
        (~1 MiB on most systems), where the numeric value indicates how many pages of
        that chunk have been allocated to the user or are stored in the batch list.
        When the value drops to zero, the entire chunk will be decommitted as part of
        the coalesce step.  This marginally increases syscall overhead during
        allocation, but is the only realistic way to work around windows commit costs
        while maintaining the benefits of virtual memory.  Note that the ledger spans
        only the public address space - the node storage uses a simple high-water mark
        instead. */
        static constexpr uint32_t CHUNK_WIDTH = std::numeric_limits<uint8_t>::max();

        /* In order to reduce syscall overhead, empty nodes are not immediately
        decommitted and returned to the free list.  Instead, they are added to a batch
        list that grows until a certain page threshold is reached, after which the
        batched nodes are sorted by offset, coalesced with adjacent free blocks, and
        decommitted en masse.  Only after this are the batched nodes moved back into
        the free list.

        The sorting algorithm is an LSD radix sort with 8-bit digits and OneSweep-style
        loop fusion, which requires two temporary buffers to store the digit counts and
        prefix sums.  Additionally, insertion sort will be used as an optimization if 
        the batch size is below a (tunable) threshold.  */
        static constexpr uint32_t COALESCE_BUFFER = VMEM_COALESCE_AFTER.pages();
        static constexpr uint32_t RADIX = 256;  // 256 bins
        static constexpr uint32_t RADIX_LOG2 = 8;  // 8-bit digits
        static constexpr uint32_t RADIX_MASK = RADIX - 1;  // for digit extraction
        static constexpr uint32_t RADIX_LAST = std::numeric_limits<uint32_t>::digits - RADIX_LOG2;

        /* Slabs are used to speed up small allocations up to a couple of pages in
        length.  These are placed in the public partition just like any other
        allocation, and will always have a fixed size of 32 pages (128 KiB on most
        systems).  They will also be aligned to that same size, meaning their starting
        addresses will always end with zeroes in the lower bits, which enables reverse
        lookup from arbitrary pointers just by applying a bitmask.  The size classes
        begin at just a single pointer, and go up to a multiple of the page size,
        increasing by 1.25x each time (before pointer alignment).

        Similar to the batch list, slabs reuse nodes to identify themselves and avoid
        reimplementing extra data structures.  Just like regular nodes, the `offset`
        indicates the beginning of the slab relative to the public partition, but the
        size is fixed to `SIZE_PAGES` at compile time, and the `size` member will
        instead be used to store the slab's current size class.  The `left` and `right`
        children are reused to store a doubly-linked list of partial slabs for each
        size class, and `heap` holds the effective size of the slab's free list.

        The data layout for each slab is as follows:

            [blocks...][...][...free][node id]

        The blocks are stored first in order to prevent any alignment issues, and will
        consist of contiguous regions of memory of the appropriate `SLAB_CLASS[i]`,
        whose number will never exceed `SLAB_BLOCKS[i]` for a given `i < SLABS`.  The
        slab's node id is anchored to the end of the allocated region, and is easily
        recoverable during reverse lookups thanks to the fixed size of each slab.  It
        is immediately preceded by the free list, which is an array of integers whose
        bit widths are determined by the `block_id` type alias, and whose size is
        bounded by `SLAB_BLOCKS[i]`, growing to the left. */
        static constexpr uint32_t SLAB_PAGES = 32;  // pages per slab (indexable by 32-bit ints)
        static constexpr capacity<> SLAB_MIN = sizeof(void*);  // min block size in bytes
        static constexpr auto _SLABS = [] -> std::pair<capacity<>, uint16_t> {
            capacity<> n = SLAB_MIN;
            capacity<> p = n;
            uint16_t i = 0;
            while (n < (PAGE_SIZE << 1)) {
                p = n;
                n = ((n * 5) / (4 * SLAB_MIN) + ((n * 5) % (4 * SLAB_MIN) != 0)) * SLAB_MIN;
                ++i;
            }
            return {p, i};
        }();
        static constexpr capacity<> SLAB_MAX = _SLABS.first;  // max block size in bytes
        static constexpr uint16_t SLABS = _SLABS.second;  // total number of size classes
        static constexpr uintptr_t SLAB_MASK =  // bitmask for reverse lookup
            ~static_cast<uintptr_t>((size_t(SLAB_PAGES) << PAGE_SHIFT) - 1);
        static constexpr uintptr_t SLAB_OFFSET =  // offset from slab start to node id
            static_cast<uintptr_t>((size_t(SLAB_PAGES) << PAGE_SHIFT) - sizeof(uint32_t));
        using block_id = std::conditional_t<  // type for slab free list entries
            SLAB_OFFSET / (SLAB_MIN + sizeof(uint8_t)) <= std::numeric_limits<uint8_t>::max(),
            uint8_t,
            std::conditional_t<
                SLAB_OFFSET / (SLAB_MIN + sizeof(uint16_t)) <= std::numeric_limits<uint16_t>::max(),
                uint16_t,
                uint32_t
            >
        >;
        static constexpr auto _BLOCKS = [] {
            std::pair<
                std::array<capacity<>, SLABS>,  // size class in bytes
                std::array<block_id, SLABS>  // max number of blocks per slab
            > result;
            capacity<> n = SLAB_MIN;
            for (size_t i = 0; i < SLABS; ++i) {
                result.first[i] = n;
                result.second[i] = (SLAB_OFFSET / (n + sizeof(block_id))).value;
                n = ((n * 5) / (4 * SLAB_MIN) + ((n * 5) % (4 * SLAB_MIN) != 0)) * SLAB_MIN;
            }
            return result;
        }();
        static constexpr const std::array<capacity<>, SLABS>& SLAB_CLASS = _BLOCKS.first;
        static constexpr const std::array<block_id, SLABS>& SLAB_BLOCKS = _BLOCKS.second;

        /* Atomic pointer back to the thread-local object that spawned this address
        space, or null if the thread is in a teardown state. */
        std::atomic<address_space_handle*> owner;

        /* High-water mark for node indices + chunks on windows. */
        uint32_t next_node = 0;
        uint32_t next_chunk = 0;

        /* Node index to root of page treap. */
        uint32_t treap_root = NIL;

        /* Number of active nodes (excluding batch/free list and slabs). */
        uint32_t heap_size = 0;

        /* Index of first unused node in storage or NIL. */
        uint32_t free = NIL;

        /* Effective size, total number of pages, min, and max offsets stored in batch
        list. */
        uint32_t batch_size = 0;
        uint32_t batch_pages = 0;
        uint32_t batch_min = NIL;
        uint32_t batch_max = NIL;

        /* Total number of bytes allocated to the user, both excluding batch list and
        empty slab blocks as well as including them. */
        std::atomic<size_t> occupied = 0;
        size_t total = 0;

        /* Remote free requests are stored in two separate lock-free MPSC stacks, one
        for slab deallocations and another for page-run deallocations.  Both stacks
        will be drained at the beginning of local `allocate()`, `deallocate()`, and
        `reserve()` operations, requiring only a single atomic load in the empty case
        for each stack.  This ensures that remote frees are always handled on their
        originating thread, without interrupting either that thread or the sending
        thread.

        Note that this implementation is optimized in 2 important ways:

            1.  The deallocated storage itself will be repurposed to store the next
                pointer for the stack, as well as a byte count for page-run frees.
                This avoids the need to allocate any extra memory for remote free
                requests.  For slab frees, the minimum size class may only be able to
                store a single pointer, so the byte count will be omitted and recovered
                from the detected size class during processing.
            2.  The stack avoids the ABA problem by simply exchanging the head pointer
                with `nullptr` during processing, ensuring that concurrent drains will
                simply see an empty stack and return immediately.

        Additionally, an in-flight counter is used to detect quiescence during the
        final teardown sequence for the address space, ensuring that all remote free
        requests have been fully processed before unmapping the address space.  This
        counter is incremented/decremented at the start/end of every allocator
        interaction, and if it drops to zero while `owner == nullptr` and
        `occupied == 0`, then it is safe to unmap. */
        struct defer_slab { defer_slab* next; };
        struct defer_page { defer_page* next; uint32_t pages; };
        std::atomic<defer_slab*> remote_slab = nullptr;
        std::atomic<defer_page*> remote_page = nullptr;
        std::atomic<size_t> in_flight = 0;

        /* An array of node ids to partial slabs (or NIL) by size class. */
        uint32_t* slabs;

        /* A temporary array storing digit counts for coalesce sorting. */
        uint32_t* radix_count;

        /* A temporary array storing prefix sums for coalesce sorting. */
        uint32_t* radix_sum;

        /* An array storing batched (but not yet freed) node ids, whose effective
        size is tracked by the `batch_size` counter.  This array will be drained by
        `coalesce()` once `batch_pages` exceeds a certain threshold. */
        uint32_t* batch_list;

        /* A temporary array for out-of-place radix sorting of the batch list.  Rather
        than copying memory back and forth, these pointers will just be swapped on
        each iteration to reduce reads and writes. */
        uint32_t* batch_sort;

        /* An array of 8-bit counters representing chunks of 255 pages in the public
        partition.  This is only needed on windows systems. */
        uint8_t* chunks;

        /* Nodes use an intrusive heap + treap data structure that compresses to just
        24 bytes per node.  In order to save space and promote cache locality, the
        max heap is stored as an overlapping array using the `heap_array` member, which
        should only ever be accessed by the `heap()` helper method for clarity.
        Lastly, the free list reuses the `heap` member on uninitialized nodes as a
        pointer to the next node in a singly-linked list. */
        struct node {
            uint32_t offset;  // start of public region, as a page offset from `address_space::ptr`
            uint32_t size;  // number of pages in region
            uint32_t left;  // left BST child index
            uint32_t right;  // right BST child index
            uint32_t heap;  // index into the heap array or next node in free list
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
        node* nodes;

        /* Look up an index in the overlapping heap array without the confusing
        `nodes[pos].heap_array` syntax. */
        [[nodiscard]] constexpr uint32_t& heap(uint32_t pos) noexcept {
            return nodes[pos].heap_array;
        }
        [[nodiscard]] constexpr const uint32_t& heap(uint32_t pos) const noexcept {
            return nodes[pos].heap_array;
        }

        /* Start of public partition. */
        void* ptr;
    };

    /* Each hardware thread has its own local address space, which it can access
    globally, without locking.  Because the address space hands out virtual memory,
    there are some differences from traditional allocators.

    First, because virtual memory is decoupled from physical memory, fragmentation is
    much less of a concern, since unoccupied regions will take up zero physical space.
    In fact, fragmentation can actually be an advantage, since it maximizes the
    probability that dynamic allocations can grow in-place without relocating any
    existing elements.  As such, we store the free list as a max heap ordered by
    overall size, and always allocate in the center of the largest available region.

    There is some complication on windows systems, however, since virtual memory must
    be explicitly committed before use, and decommitted when no longer needed.  As
    such, regions will be committed in chunks of 255 pages (~1 MiB on most systems) at
    a time in order to minimize syscall overhead, and decommitted whenever their
    occupied count falls to zero.  This process can be avoided on unix because they
    lack a hard commit limit, and only fail when the system runs out of physical
    memory. */
    struct address_space : address_space_base {
        /* The address space is separated into a private and public partition, with
        the size of each determined by the total `VMEM_PER_THREAD` specified at compile
        time.  The private partition begins with the `address_space` header itself,
        followed by the heads of each of the slab size class lists, two small buffers
        to hold the radix counts and prefix sums for coalesce sorting, and then the
        coalesce buffer and a temporary sorting buffer.  On windows, this is followed
        by a commit chunk ledger that allows fine-grained commit and decommit of pages
        in the public partition.  The rest of the private partition is devoted to node
        storage and overlapping max heap, which holds enough nodes to uniquely
        represent every page in the public partition.  Because each node only consumes
        24 bytes on average (plus some extra for internal data structures), and pages
        are generally 4 KiB in the worst case, this private partition will require only
        ~0.6% of a sufficiently large address space, and will be able to represent up
        to ~16 TiB of address space per thread.  This becomes more efficient as the
        page size increases. */
        static constexpr struct _SIZE {
            static constexpr capacity<> HEADER = sizeof(address_space_base);
            static constexpr capacity<> SLABS =
                capacity(address_space::SLABS * sizeof(uint32_t)).align_up(sizeof(void*));
            static constexpr capacity<> RADIX_COUNT =
                capacity(address_space::RADIX * sizeof(uint32_t)).align_up(sizeof(void*));
            static constexpr capacity<> RADIX_SUM = RADIX_COUNT;
            static constexpr capacity<> BATCH =
                capacity(COALESCE_BUFFER * sizeof(uint32_t)).align_up(sizeof(void*));
            static constexpr capacity<> BATCH_SORT = BATCH;

            uint32_t chunks;  // size of chunk ledger in 1 byte entries, which represent 255 pages
            uint32_t partition;  // total size of private partition in pages
            uint32_t pub;  // size of public partition in pages, equivalent to max number of nodes

            /* The total amount of virtual memory consumed by the private and public
            partitions combined. */
            [[nodiscard]] constexpr capacity<> total() const noexcept {
                return size_t(partition + pub) << PAGE_SHIFT;
            }

            /* Size of the initial commit necessary to set up the private partition
            data structures, excluding node storage. */
            [[nodiscard]] constexpr capacity<> commit() const noexcept {
                return (
                    HEADER +
                    SLABS +
                    RADIX_COUNT +
                    RADIX_SUM +
                    BATCH +
                    BATCH_SORT +
                    capacity(chunks * sizeof(uint8_t)).align_up(sizeof(void*))
                ).pages() << PAGE_SHIFT;
            }
        } SIZE = [] -> _SIZE {
            // initial upper bound -> x * (page + node) <= VMEM_PER_THREAD
            uint32_t lo = 1;
            uint32_t hi = (VMEM_PER_THREAD / (PAGE_SIZE + sizeof(node))).value;

            // binary search for maximum N where partition(N) + (N * page) <= VMEM_PER_THREAD
            uint32_t chunks = WINDOWS;  // 1 if on windows
            uint32_t partition = (
                _SIZE::HEADER +
                _SIZE::SLABS +
                _SIZE::RADIX_COUNT +
                _SIZE::RADIX_SUM +
                _SIZE::BATCH +
                _SIZE::BATCH_SORT +
                capacity(chunks * sizeof(uint8_t)).align_up(sizeof(void*)) +
                sizeof(node)
            ).pages();
            while (lo < hi) {
                uint32_t mid = (hi + lo + 1) / 2;
                if constexpr (WINDOWS) {  // account for chunk ledger
                    chunks = mid / CHUNK_WIDTH + (mid % CHUNK_WIDTH != 0);
                }
                partition = capacity<>(
                    (
                        _SIZE::HEADER +
                        _SIZE::SLABS +
                        _SIZE::RADIX_COUNT +
                        _SIZE::RADIX_SUM +
                        _SIZE::BATCH +
                        _SIZE::BATCH_SORT +
                        capacity(chunks * sizeof(uint8_t)).align_up(sizeof(void*))
                    ) +
                    (mid * sizeof(node))
                ).pages();
                if (
                    partition <= (VMEM_PER_THREAD >> PAGE_SHIFT) &&
                    mid <= (VMEM_PER_THREAD >> PAGE_SHIFT) - partition
                ) {
                    lo = mid;
                } else {
                    hi = mid - 1;
                }
            }
            return {chunks, partition, lo};
        }();
        static_assert(
            VMEM_PER_THREAD == 0 ||
            VMEM_PER_THREAD >= (size_t(SIZE.partition + SIZE.pub) << PAGE_SHIFT),
            "VMEM_PER_THREAD is too small to hold internal data structures.  Either "
            "recompile with a larger `VMEM_PER_THREAD`, set it to zero to disable "
            "virtual memory, or reduce `COALESCE_AFTER` to force more frequent "
            "decommits."
        );

        /* Initialize a single free node covering the entire public partition on
        construction. */
        [[nodiscard]] address_space(address_space_handle* owner) noexcept : address_space_base{
            .owner = owner,
            .slabs = reinterpret_cast<uint32_t*>(
                reinterpret_cast<std::byte*>(this) +
                _SIZE::HEADER.bytes()
            ),
            .radix_count = reinterpret_cast<uint32_t*>(
                reinterpret_cast<std::byte*>(this) +
                _SIZE::HEADER.bytes() +
                _SIZE::SLABS.bytes()
            ),
            .radix_sum = reinterpret_cast<uint32_t*>(
                reinterpret_cast<std::byte*>(this) +
                _SIZE::HEADER.bytes() +
                _SIZE::SLABS.bytes() +
                _SIZE::RADIX_COUNT.bytes()
            ),
            .batch_list = reinterpret_cast<uint32_t*>(
                reinterpret_cast<std::byte*>(this) +
                _SIZE::HEADER.bytes() +
                _SIZE::SLABS.bytes() +
                _SIZE::RADIX_COUNT.bytes() +
                _SIZE::RADIX_SUM.bytes()
            ),
            .batch_sort = reinterpret_cast<uint32_t*>(
                reinterpret_cast<std::byte*>(this) +
                _SIZE::HEADER.bytes() +
                _SIZE::SLABS.bytes() +
                _SIZE::RADIX_COUNT.bytes() +
                _SIZE::RADIX_SUM.bytes() +
                _SIZE::BATCH.bytes()
            ),
            .chunks = reinterpret_cast<uint8_t*>(
                reinterpret_cast<std::byte*>(this) +
                _SIZE::HEADER.bytes() +
                _SIZE::SLABS.bytes() +
                _SIZE::RADIX_COUNT.bytes() +
                _SIZE::RADIX_SUM.bytes() +
                _SIZE::BATCH.bytes() +
                _SIZE::BATCH_SORT.bytes()
            ),
            .nodes = reinterpret_cast<node*>(
                reinterpret_cast<std::byte*>(this) +
                _SIZE::HEADER.bytes() +
                _SIZE::SLABS.bytes() +
                _SIZE::RADIX_COUNT.bytes() +
                _SIZE::RADIX_SUM.bytes() +
                _SIZE::BATCH.bytes() +
                _SIZE::BATCH_SORT.bytes() +
                capacity(SIZE.chunks * sizeof(uint8_t)).align_up(sizeof(void*)).bytes()
            ),
            .ptr = reinterpret_cast<void*>(
                reinterpret_cast<std::byte*>(this) +
                (size_t(SIZE.partition) << PAGE_SHIFT)
            )
        } {
            std::fill_n(slabs, SLABS, NIL);
            uint32_t id = get_node();  // always zero on first init, commits node storage on windows
            nodes[id].offset = 0;  // start of public partition
            nodes[id].size = SIZE.pub;  // whole public partition
            nodes[id].left = NIL;  // no children
            nodes[id].right = NIL;  // no children
            nodes[id].heap = 0;  // only node in heap
            heap(id) = 0;  // only heap entry
            heap_size = 1;  // only active node
            treap_root = id;  // root of coalesce tree
        }

        ////////////////////
        ////    HEAP    ////
        ////////////////////

        /* Ensure that the heap invariant is fulfilled during debug builds. */
        constexpr void heap_validate() const {
            for (uint32_t i = 0; i < heap_size; ++i) {
                uint32_t id = heap(i);
                uint32_t left = (i * 2) + 1;
                uint32_t right = left + 1;
                if (left < heap_size && nodes[heap(left)].size > nodes[id].size) {
                    throw MemoryError(
                        "heap invariant violated: left child larger than parent"
                    );
                }
                if (right < heap_size && nodes[heap(right)].size > nodes[id].size) {
                    throw MemoryError(
                        "heap invariant violated: right child larger than parent"
                    );
                }
            }
        }

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
                uint32_t best = pos;
                if (
                    left < heap_size &&
                    nodes[heap(left)].size > nodes[heap(best)].size
                ) best = left;
                if (
                    right < heap_size &&
                    nodes[heap(right)].size > nodes[heap(best)].size
                ) best = right;
                if (best == pos) break;
                heap_swap(pos, best);
                pos = best;
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
        constexpr void heap_insert(uint32_t id) noexcept (!VMEM_DEBUG) {
            heap(heap_size) = id;
            nodes[id].heap = heap_size;
            heapify_up(heap_size);
            ++heap_size;
            if constexpr (VMEM_DEBUG) {
                heap_validate();
            }
        }

        /* Remove the current node from the heap, maintaining the max heap
        invariant. */
        constexpr void heap_erase(uint32_t id) noexcept (!VMEM_DEBUG) {
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
            if constexpr (VMEM_DEBUG) {
                heap_validate();
            }
        }

        /* Restore the heap invariant for the current node, assuming it is still in
        the heap. */
        constexpr void heap_fix(uint32_t id) noexcept (!VMEM_DEBUG) {
            uint32_t pos = nodes[id].heap;
            heapify_down(pos);
            heapify_up(pos);
            if constexpr (VMEM_DEBUG) {
                heap_validate();
            }
        }

        /////////////////////
        ////    TREAP    ////
        /////////////////////

        /* Ensure that the treap invariant is fulfilled during debug builds. */
        constexpr void treap_validate(uint32_t root) const {
            if (root == NIL) {
                return;
            }
            uint32_t left = nodes[root].left;
            uint32_t right = nodes[root].right;
            if (left != NIL) {
                if (nodes[left].offset >= nodes[root].offset) {
                    throw MemoryError(
                        "treap invariant violated: left child not less than parent"
                    );
                }
                if (nodes[left].priority() < nodes[root].priority()) {
                    throw MemoryError(
                        "treap invariant violated: left child priority less than parent"
                    );
                }
                treap_validate(left);
            }
            if (right != NIL) {
                if (nodes[right].offset < nodes[root].offset) {
                    throw MemoryError(
                        "treap invariant violated: right child not greater than or "
                        "equal to parent"
                    );
                }
                if (nodes[right].priority() < nodes[root].priority()) {
                    throw MemoryError(
                        "treap invariant violated: right child priority less than parent"
                    );
                }
                treap_validate(right);
            }
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
        [[nodiscard]] constexpr uint32_t treap_insert(uint32_t root, uint32_t id)
            noexcept (!VMEM_DEBUG)
        {
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
            if constexpr (VMEM_DEBUG) {
                treap_validate(root);
            }
            return root;
        }

        /* Iteratively search for a key in the treap and remove it while maintaining
        sorted order.  Note that the key must compare equal to exactly one entry in the
        treap.  The return value is the new root of the treap, and may be identical to
        the previous root.  Note this is done without an explicit stack via a simple
        pointer, which modifies links in-place by rotating down the tree. */
        [[nodiscard]] constexpr uint32_t treap_erase(uint32_t root, uint32_t id)
            noexcept (!VMEM_DEBUG)
        {
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
            if constexpr (VMEM_DEBUG) {
                treap_validate(root);
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

        /* Find the free block starting at or immediately following the given key in
        the treap. */
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

        //////////////////////////////
        ////    NODES + CHUNKS    ////
        //////////////////////////////

        /* Get a node id from the free list or end of storage.  Note that this does
        not insert the node into either the heap or treap, or allocate any memory. */
        [[nodiscard]] constexpr uint32_t get_node() noexcept {
            // pop from free list if possible
            if (free != NIL) {
                uint32_t id = free;
                free = nodes[id].heap;  // next free node or NIL
                return id;
            }

            // commit more node storage if on windows (unnecessary for unix systems,
            // since they lack a hard commit limit).  We do this multiple pages at a
            // time to ensure that we always end at a page boundary or the end of the
            // node storage
            if constexpr (WINDOWS) {
                constexpr size_t lcm = std::lcm(sizeof(node), PAGE_SIZE.value);
                if (next_node >= next_chunk) {
                    size_t commit = std::min(lcm, size_t(SIZE.pub - next_node) << PAGE_SHIFT);
                    commit_address_space(nodes + next_node, commit);
                    next_chunk += commit / sizeof(node);
                }
            }
            return next_node++;
        }

        /* Push a node onto the free list.  Note that this does not remove the node
        from either the heap or treap, or deallocate any memory. */
        constexpr void put_node(uint32_t id) noexcept {
            nodes[id].heap = free;
            free = id;
        }

        /* Register a committed region starting at the public `offset` and continuing
        for `size` pages in the chunk ledger (assuming it is present). */
        constexpr void chunk_increment(uint32_t offset, uint32_t size) noexcept {
            // set partial chunk from first page to end of chunk
            uint32_t i = offset / CHUNK_WIDTH;
            uint32_t n = std::min(size, CHUNK_WIDTH - (offset % CHUNK_WIDTH));
            chunks[i++] += n;
            size -= n;
            while (size > 0) {
                // set partial chunk from start of chunk to last page
                if (size < CHUNK_WIDTH) {
                    chunks[i] += static_cast<uint8_t>(size);
                    break;
                }

                // fill intermediate chunks
                chunks[i++] = CHUNK_WIDTH;
                size -= CHUNK_WIDTH;
            }
        }

        /* Register a decommitted region starting at the public `offset` and continuing
        for `size` pages in the chunk ledger (assuming it is present). */
        constexpr void chunk_decrement(uint32_t offset, uint32_t size) noexcept {
            // clear partial chunk from first page to end of chunk
            uint32_t i = offset / CHUNK_WIDTH;
            uint32_t n = std::min(size, CHUNK_WIDTH - (offset % CHUNK_WIDTH));
            chunks[i++] -= n;
            size -= n;
            while (size > 0) {
                // clear partial chunk from start of chunk to last page
                if (size < CHUNK_WIDTH) {
                    chunks[i] -= static_cast<uint8_t>(size);
                    break;
                }

                // clear intermediate chunks
                chunks[i++] = 0;
                size -= CHUNK_WIDTH;
            }
        }

        /* Given a public offset and a number of pages, get a [start, stop) interval
        over the empty or full chunks between them, for commit/decommit purposes. */
        [[nodiscard]] constexpr auto chunk_interval(uint32_t offset, uint32_t size) noexcept
            -> std::pair<uint32_t, uint32_t>
        {
            uint32_t from = offset / CHUNK_WIDTH;
            from += chunks[from] > 0;
            from *= CHUNK_WIDTH;
            uint32_t s = offset + size;
            uint32_t to = s / CHUNK_WIDTH;
            to += (s % CHUNK_WIDTH) != 0 && chunks[to] == 0;
            to = std::min(to * CHUNK_WIDTH, SIZE.pub);
            return {from, to};
        }

        /* Commit the chunks necessary to store a public allocation starting at
        `offset` and continuing for `size` pages.  Note that this is separate from
        `chunk_increment()`, which must be called after this to ensure correct interval
        calculations. */
        constexpr void chunk_commit(uint32_t offset, uint32_t size) {
            auto [from, to] = chunk_interval(offset, size);
            if (to > from) {
                commit_address_space(
                    static_cast<std::byte*>(ptr) + (size_t(from) << PAGE_SHIFT),
                    size_t(to - from) << PAGE_SHIFT
                );
            }
        }

        /* Decommit the chunks necessary to free a public allocation starting at
        `offset` and continuing for `size` pages.  Note that this is separate from
        `chunk_decrement()`, which must be called before this to ensure correct
        interval calculations. */
        constexpr void chunk_decommit(uint32_t offset, uint32_t size) {
            auto [from, to] = chunk_interval(offset, size);
            if (to > from) {
                decommit_address_space(
                    static_cast<std::byte*>(ptr) + (size_t(from) << PAGE_SHIFT),
                    size_t(to - from) << PAGE_SHIFT
                );
            };
        }

        /////////////////////
        ////    PAGES    ////
        /////////////////////

        /* Find the largest free region in the allocator that can accomodate an
        allocation of `pages` with the given `alignment`.  Returns a pair where the
        first element is the bisected node id, and the second element is the distance
        from that node's offset to the start of the allocated region, in pages.  If
        no suitable region could be found, then the node id will be equal to `NIL`. */
        [[nodiscard]] constexpr std::pair<uint32_t, uint32_t> page_bisect(
            uint64_t pos,
            uint32_t pages,
            capacity<> alignment,
            capacity<> base_mod
        ) const noexcept {
            // if current heap entry doesn't have enough space to fit the allocation,
            // then none of its children will either, and we can terminate recursion
            if (pos >= heap_size) return {NIL, 0};
            uint32_t id = heap(static_cast<uint32_t>(pos));
            if (nodes[id].size < pages) return {NIL, 0};

            // calculate initial left boundary, then adjust to account for alignment
            uint32_t left = (nodes[id].size - pages) >> 1;
            if (alignment > 1) {
                // round downward to nearest aligned address
                size_t tmp = base_mod.value + nodes[id].offset + left;
                tmp &= ~(alignment.value - 1);  // alignment is still a power of two, but in pages
                tmp -= base_mod.value;

                // if aligned address is before start of block, increment by alignment
                if (tmp < nodes[id].offset) tmp += alignment.value;

                // if aligned address is still outside of block, search children
                if (tmp < nodes[id].offset || tmp + pages > nodes[id].end()) {
                    pos = (pos << 1) + 1;

                    // return larger of two results or NIL if both failed
                    auto r1 =
                        page_bisect(pos, pages, alignment, base_mod);
                    auto r2 =
                        page_bisect(pos + 1, pages, alignment, base_mod);
                    return (r2.first == NIL || (
                        r1.first != NIL && nodes[r1.first].size >= nodes[r2.first].size
                    )) ? r1 : r2;
                }

                // update left offset to aligned address
                if constexpr (VMEM_DEBUG) {
                    if (tmp > std::numeric_limits<uint32_t>::max()) {
                        throw MemoryError("aligned allocation offset overflows uint32_t");
                    }
                }
                left = static_cast<uint32_t>(tmp) - nodes[id].offset;
            }

            return {id, left};
        }

        /* Request `pages` worth of memory from the address space, returning an offset
        to the start of the allocated region of the public partition, and updating the
        internal data structures to reflect the allocation.  Note that the result of
        this method must eventually be passed to `deallocate()` along with its current
        length in pages in order to release memory back to the address space. */
        [[nodiscard]] uint32_t page_allocate(uint32_t pages, capacity<> alignment) {
            if constexpr (VMEM_DEBUG) {
                if (pages == 0) {
                    throw MemoryError("attempted to allocate zero pages from address space");
                }
                if (std::popcount(alignment.value) != 1) {
                    throw MemoryError("alignment must be a power of two");
                }
            }

            // find largest free region that can accomodate aligned allocation
            auto [id, left] = page_bisect(
                0,
                pages,
                alignment >> PAGE_SHIFT,
                (reinterpret_cast<uintptr_t>(ptr) & (alignment.value - 1)) >> PAGE_SHIFT
            );
            if (id == NIL) {
                if (batch_size > 0) {
                    coalesce();
                    return page_allocate(pages, alignment);  // try again
                }
                throw MemoryError(std::format(
                    "failed to allocate {:.3f} MiB from local address space ({:.3f} "
                    "MiB / {:.3f} MiB - {:.3f}%) -> insufficient space available",
                    double(size_t(pages) << PAGE_SHIFT) / double((1_MiB).value),
                    double(total) / double((1_MiB).value),
                    double(SIZE.total().value) / double((1_MiB).value),
                    (double(total) / double(size_t(SIZE.pub) << PAGE_SHIFT)) * 100.0
                ));
            }
            uint32_t right = nodes[id].size - pages - left;
            uint32_t offset = nodes[id].offset + left;
            if constexpr (VMEM_DEBUG) {
                if ((
                    reinterpret_cast<uintptr_t>(ptr) + (size_t(offset) << PAGE_SHIFT)
                ) % alignment != 0) {
                    throw MemoryError("allocated region is not properly aligned");
                }
            }

            // commit the allocated region on windows, aligning to chunk boundaries
            // (unnecessary for unix systems, since they lack a hard commit limit).
            // Note that the chunk counters have not been updated yet, which is crucial
            // for the interval calculation
            if constexpr (WINDOWS) {  
                chunk_commit(offset, pages);
                chunk_increment(offset, pages);
            }

            // if there is one remaining half, reuse the existing node without creating
            // a new one; otherwise, get a new node or push the current node onto the
            // free list
            if (left) {
                // no need to fix treap, since the left half never moved
                nodes[id].size = left;
                heap_fix(id);
                if (right) {
                    uint32_t child = get_node();
                    node& c = nodes[child];
                    c.offset = offset + pages;
                    c.size = right;
                    c.left = NIL;
                    c.right = NIL;
                    c.heap = NIL;
                    treap_root = treap_insert(treap_root, child);
                    heap_insert(child);
                }
            } else if (right) {
                treap_root = treap_erase(treap_root, id);
                nodes[id].offset = offset + pages;
                nodes[id].size = right;
                treap_root = treap_insert(treap_root, id);
                heap_fix(id);
            } else {
                heap_erase(id);
                treap_root = treap_erase(treap_root, id);
                put_node(id);
            }

            // return offset to allocated region
            size_t delta = size_t(pages) << PAGE_SHIFT;
            occupied.fetch_add(delta, std::memory_order_release);
            total += delta;
            return offset;
        }

        /* Apply insertion sort to batches with size less than `INSERTION_THRESHOLD` to
        mitigate constant factors in radix sort. */
        constexpr void insertion_sort() noexcept {
            for (uint32_t i = 1; i < batch_size; ++i) {
                if (nodes[batch_list[i]].offset < nodes[batch_list[i - 1]].offset) {
                    uint32_t tmp = batch_list[i];
                    batch_list[i] = batch_list[i - 1];
                    uint32_t j = i - 1;
                    while (j > 0 && nodes[tmp].offset < nodes[batch_list[j - 1]].offset) {
                        batch_list[j] = batch_list[j - 1];
                        --j;
                    }
                    batch_list[j] = tmp;
                }
            }
        }

        /* Radix sort the batch list by offset, updating it in-place.  This algorithm
        incorporates an innovation from OneSweep: A Faster Least Significant Digit
        Radix Sort for GPUs (Andy Adinets and Duane Merrill, 2022).

            https://arxiv.org/abs/2206.01784

        This effectively combines counting for the next pass with scatter for the
        current pass, reducing the total number of iterations from `O(2d(n + k))` to
        `O((d + 1)(n + k))`, where `d` is the number of digits per key (4 for 32-bit
        integers), `n` is the number of elements, and `k` is the number of bins (256
        for 8-bit digits).

        Here's some additional reading material for those interested, which may inspire
        future optimizations:

            https://travisdowns.github.io/blog/2019/05/22/sorting.html
            https://duvanenko.tech.blog/2022/04/10/in-place-n-bit-radix-sort/
            https://github.com/KirillLykov/int-sort-bmk?tab=readme-ov-file
            https://stackoverflow.com/questions/28884387/how-is-the-most-significant-bit-radix-sort-more-efficient-than-the-least-signifi
        */
        constexpr void radix_sort() noexcept {
            if (batch_size < 2) {
                return;
            }
            if (batch_size <= VMEM_INSERTION_THRESHOLD) {
                insertion_sort();
                return;
            }

            // initial counting to determine bin sizes/offsets for first digit
            capacity(RADIX * sizeof(uint32_t)).clear(radix_count);
            for (uint32_t i = 0; i < batch_size; ++i) {
                ++radix_count[nodes[batch_list[i]].offset & RADIX_MASK];
            }

            // compute prefix sums
            uint32_t prefix = 0;
            for (uint32_t i = 0; i < RADIX; ++i) {
                uint32_t n = radix_count[i];
                radix_sum[i] = prefix;  // starting index for this bin
                prefix += n;
            }

            // sort ids into bins by offset digit, least significant first
            for (uint32_t shift = 0; shift < RADIX_LAST; shift += RADIX_LOG2) {
                capacity(RADIX * sizeof(uint32_t)).clear(radix_count);

                // scatter combined with counting for next pass
                for (uint32_t i = 0; i < batch_size; ++i) {
                    uint32_t key = nodes[batch_list[i]].offset >> shift;
                    batch_sort[radix_sum[key & RADIX_MASK]++] = batch_list[i];
                    ++radix_count[(key >> RADIX_LOG2) & RADIX_MASK];
                }

                // recompute prefix sums for next pass
                prefix = 0;
                for (uint32_t i = 0; i < RADIX; ++i) {
                    uint32_t n = radix_count[i];
                    radix_sum[i] = prefix;  // starting index for next pass
                    prefix += n;
                }

                // swap buffers
                std::swap(batch_list, batch_sort);
            }

            // final pass can avoid simultaneous counting/prefix sum
            for (uint32_t i = 0; i < batch_size; ++i) {
                uint32_t key = nodes[batch_list[i]].offset;
                batch_sort[radix_sum[(key >> RADIX_LAST) & RADIX_MASK]++] = batch_list[i];
            }

            // ensure the sorted data is kept in the original batch array
            std::swap(batch_list, batch_sort);
        }

        /* Extend an initial free run to the right, merging with adjacent nodes from
        either the batch list or treap.  Any merged nodes will be appended to the free
        list, and those that originate from the treap will be removed from both the
        heap and treap.  If no adjacent nodes are found, then the run is complete. */
        [[nodiscard]] bool cascade(uint32_t run, uint32_t& i) {
            uint32_t j;

            // prefer merging with batch nodes first, pushing them to the free list
            if (i < batch_size) {
                j = batch_list[i];
                if (nodes[run].end() == nodes[j].offset) {
                    nodes[run].size += nodes[j].size;
                    put_node(j);
                    ++i;
                    if constexpr (WINDOWS) {
                        chunk_decrement(nodes[j].offset, nodes[j].size);
                    }
                    return true;
                }
            }

            // fall back to treap nodes if possible, removing from heap + treap and
            // pushing to free list
            j = treap_next(treap_root, nodes[run].end());
            if (j != NIL && nodes[run].end() == nodes[j].offset) {
                nodes[run].size += nodes[j].size;
                heap_erase(j);
                treap_root = treap_erase(treap_root, j);
                put_node(j);
                return true;
            }

            // run is complete
            return false;
        }

        /* Purge the `batch_list` and coalesce adjacent free blocks, informing the
        operating system that the physical memory backing them can be reclaimed. */
        void coalesce() {
            radix_sort();

            // process each run in the sorted batch list, assuming there is at least one
            uint32_t i = 0;
            do {
                uint32_t run = batch_list[i++];
                bool insert = false;
                if constexpr (WINDOWS) {
                    chunk_decrement(nodes[run].offset, nodes[run].size);
                }

                // for the first node in each run, check for an adjacent predecessor
                // that's already in the treap or remember to insert if not
                uint32_t n = treap_prev(treap_root, nodes[run].offset);
                if (n != NIL && nodes[n].end() == nodes[run].offset) {
                    nodes[n].size += nodes[run].size;
                    put_node(run);
                    run = n;
                } else {
                    insert = true;
                }

                // cascade to the right, merging with adjacent nodes from the batch
                // list or treap until no more adjacent nodes are found
                while (cascade(run, i));

                // decommit the merged block, aligning to chunk boundaries on windows.
                // Note that the chunk values have already been updated by this point,
                // which is crucial to the interval calculation
                if constexpr (WINDOWS) {
                    chunk_decommit(nodes[run].offset, nodes[run].size);
                } else {  // on unix, just decommit the whole block
                    decommit_address_space(
                        static_cast<std::byte*>(ptr) + (size_t(nodes[run].offset) << PAGE_SHIFT),
                        size_t(nodes[run].size) << PAGE_SHIFT
                    );
                }
                total -= size_t(nodes[run].size) << PAGE_SHIFT;  // update total only after decommit

                // reinsert the merged block into the treap and/or heap if needed
                if (insert) {
                    treap_root = treap_insert(treap_root, run);
                    heap_insert(run);
                } else {
                    heap_fix(run);
                }
            } while (i < batch_size);

            // reset batch sizes + min/max, but don't bother zeroing out the arrays
            batch_size = 0;
            batch_pages = 0;
            batch_min = NIL;
            batch_max = NIL;
        }

        /* Release `pages` worth of memory back to the address space, starting at the
        given `offset` from the public partition.  Note that `offset` and `pages` must
        correspond to a previous call to `allocate()`, but may refer to only a subset
        of the originally allocated region, effectively shrinking an existing
        allocation.  No validation is performed to ensure this, however. */
        void page_deallocate(uint32_t offset, uint32_t pages) {
            uint32_t id = get_node();
            nodes[id].offset = offset;
            nodes[id].size = pages;
            nodes[id].left = NIL;
            nodes[id].right = NIL;
            nodes[id].heap = NIL;
            occupied.fetch_sub(size_t(pages) << PAGE_SHIFT, std::memory_order_release);
            batch_list[batch_size++] = id;
            batch_pages += nodes[id].size;
            if (batch_min == NIL || nodes[id].offset < batch_min) batch_min = nodes[id].offset;
            if (batch_max == NIL || nodes[id].offset > batch_max) batch_max = nodes[id].offset;
            if (batch_pages >= COALESCE_BUFFER) coalesce();
        }

        /* Extend an allocation to the right by the specified number of `pages`.
        Returns the number of bytes that were acquired if the growth was successful, or
        zero if it requires relocation (in which case no changes will be made to the
        allocator and a future call to `page_allocate()` is necessary).  Note that
        `offset` must point to the end of an allocated region, and `pages` indicates
        how many additional pages should be allocated. */
        [[nodiscard]] capacity<> page_reserve(uint32_t offset, uint32_t pages) {
            // search the treap for an adjacent free block
            uint32_t id = treap_next(treap_root, offset);
            if (id != NIL && nodes[id].offset == offset && nodes[id].size >= pages) {
                if constexpr (WINDOWS) {
                    chunk_commit(offset, pages);
                    chunk_increment(offset, pages);
                }
                treap_root = treap_erase(treap_root, id);
                nodes[id].size -= pages;
                if (nodes[id].size == 0) {
                    heap_erase(id);
                    put_node(id);
                } else {
                    nodes[id].offset += pages;
                    treap_root = treap_insert(treap_root, id);
                    heap_fix(id);
                }
                size_t result = size_t(pages) << PAGE_SHIFT;
                occupied.fetch_add(result, std::memory_order_release);
                total += result;
                return result;
            }

            // no adjacent free block of appropriate size was found.  It's possible
            // that the requested section is present in the batch list, in which case
            // we can attempt to coalesce and retry.
            if (batch_size > 0 && offset >= batch_min && offset <= batch_max) {
                coalesce();
                return page_reserve(offset, pages);
            }
            return 0;
        }

        /////////////////////
        ////    SLABS    ////
        /////////////////////

        /* Binary search for the best size class to represent an allocation of `size`
        bytes.  Note that the size must be less than or equal to `SLAB_MAX`, and the
        return value is an index into the `SLAB_CLASS`, `SLAB_BLOCKS`, and `slabs`
        arrays. */
        [[nodiscard]] static constexpr uint16_t slab_class(
            capacity<> bytes,
            capacity<> alignment
        ) noexcept {
            if constexpr (VMEM_DEBUG) {
                if (std::popcount(alignment.value) != 1) {
                    throw MemoryError("alignment must be a power of two");
                }
            }

            // binary search for smallest size class that fits request
            uint16_t lo = 0;
            uint16_t hi = SLABS;
            while (lo < hi) {
                uint16_t mid = (hi + lo) >> 1;
                if (SLAB_CLASS[mid] < bytes) {
                    lo = mid + 1;
                } else {
                    hi = mid;
                }
            }

            // ensure alignment
            --alignment;
            while (lo < SLABS && (SLAB_CLASS[lo] & alignment) != 0) ++lo;
            return lo;
        }

        /* Recover the enclosing slab's node id from an arbitrary pointer, accounting
        for slab layout. */
        [[nodiscard]] static uint32_t slab(void* p) noexcept {
            return *reinterpret_cast<uint32_t*>(
                (reinterpret_cast<uintptr_t>(p) & SLAB_MASK) + SLAB_OFFSET
            );
        }

        /* Push a block onto a slab's free list. */
        void slab_push(uint32_t slab, block_id block) noexcept {
            std::construct_at(
                reinterpret_cast<block_id*>(
                    static_cast<std::byte*>(ptr) +
                    (size_t(nodes[slab].offset) << PAGE_SHIFT) +
                    SLAB_OFFSET
                ) - (++nodes[slab].heap),
                block
            );
        }

        /* Remove a block from a slab's free list and return its index. */
        [[nodiscard]] block_id slab_pop(uint32_t slab) noexcept {
            return *(reinterpret_cast<block_id*>(
                static_cast<std::byte*>(ptr) +
                (size_t(nodes[slab].offset) << PAGE_SHIFT) +
                SLAB_OFFSET
            ) - (nodes[slab].heap--));
        }

        /* Convert a block index within a slab into a pointer to the start of that
        block's memory, accounting for layout. */
        [[nodiscard]] std::byte* slab_data(uint32_t slab, block_id block) noexcept {
            return
                static_cast<std::byte*>(ptr) +
                (size_t(nodes[slab].offset) << PAGE_SHIFT) +
                (block * SLAB_CLASS[nodes[slab].size].value);
        }

        /* Convert a pointer to a block's memory into its index within a slab,
        accounting for layout. */
        [[nodiscard]] block_id slab_block(uint32_t slab, void* p) noexcept {
            return static_cast<block_id>(
                (static_cast<std::byte*>(p) - slab_data(slab, 0)) /
                SLAB_CLASS[nodes[slab].size].value
            );
        }

        /* Allocate a block from a slab with the given size class index, returning a
        pointer to an uninitialized block within a corresponding slab.  This may
        attempt to allocate a new slab if none are available. */
        [[nodiscard]] void* slab_allocate(uint16_t size_class) {
            // if there are no partial slabs for the given size class, allocate a
            // new one
            uint32_t s = slabs[size_class];
            if (s == NIL) {
                // slabs are always aligned such that their starting address ends with all zeroes
                uint32_t offset = page_allocate(
                    SLAB_PAGES,
                    size_t(SLAB_PAGES) << PAGE_SHIFT
                );
                // don't count empty slab space
                occupied.fetch_sub(
                    size_t(SLAB_PAGES) << PAGE_SHIFT,
                    std::memory_order_release
                );

                // allocate tracking node
                s = get_node();
                nodes[s].offset = offset;  // starting page offset
                nodes[s].size = size_class;  // size class index
                nodes[s].left = NIL;  // previous partial slab
                nodes[s].right = NIL;  // next partial slab
                nodes[s].heap = SLAB_BLOCKS[size_class];  // effective free list size

                // initialize node id and free list such that `slab_pop()` returns
                // blocks in increasing order
                block_id* free_list = reinterpret_cast<block_id*>(std::construct_at(
                    reinterpret_cast<uint32_t*>(
                        static_cast<std::byte*>(ptr) +
                        (size_t(offset) << PAGE_SHIFT) +
                        SLAB_OFFSET
                    ),
                    s
                )) - SLAB_BLOCKS[size_class];
                for (block_id i = 0; i < SLAB_BLOCKS[size_class]; ++i) {
                    std::construct_at(free_list + i, i);
                }

                // insert into partial list
                slabs[size_class] = s;
            }

            // follow link to first partial slab for this size class
            if constexpr (VMEM_DEBUG) {
                if (nodes[s].heap == 0) {
                    throw MemoryError("attempted to allocate from a fully-occupied slab");
                }
            }

            // if the slab would be empty after this allocation, pop it from the
            // partial list
            if (nodes[s].heap == 1) {
                if (nodes[s].right != NIL) {
                    nodes[nodes[s].right].left = nodes[s].left;
                }
                slabs[size_class] = nodes[s].right;
                nodes[s].right = NIL;
                nodes[s].left = NIL;
            }

            // pop a block from the slab's free list and return a tagged pointer to
            // uninitialized data
            occupied.fetch_add(SLAB_CLASS[size_class].value, std::memory_order_release);
            return slab_data(s, slab_pop(s));
        }

        /* Given an allocation beginning at `p` and continuing for `bytes`, where
        `bytes <= SLAB_MAX` and `p` originates from a slab in this allocator, return it
        to the appropriate slab's free list.  If this renders a slab empty, then it may
        be deallocated to save memory unless it is the only entry of its size class.  
        Note that `bytes` is only used for debugging purposes, since the proper size is
        always determined by the slab's detected size class. */
        void slab_deallocate(void* p, capacity<>& bytes) {
            // search for a slab containing `p` via the slab treap
            uint32_t s = slab(p);
            if constexpr (VMEM_DEBUG) {
                if (s == NIL) {
                    throw MemoryError("no slab found for deallocation pointer");
                }
            }

            // follow link to detected non-empty slab
            if constexpr (VMEM_DEBUG) {
                if (nodes[s].heap == SLAB_BLOCKS[nodes[s].size]) {
                    throw MemoryError("attempted to deallocate from an empty slab");
                }
                if (
                    static_cast<std::byte*>(p) < slab_data(s, 0) ||
                    static_cast<std::byte*>(p) >= slab_data(s, SLAB_BLOCKS[nodes[s].size])
                ) {
                    throw MemoryError("block pointer is out of bounds");
                }
                if (static_cast<std::byte*>(p) != slab_data(s, slab_block(s, p))) {
                    throw MemoryError("pointer does not align with slab block boundary");
                }
                if (bytes > SLAB_CLASS[nodes[s].size]) {
                    throw MemoryError(
                        "attempted to deallocate more than 1 block's worth of bytes from slab"
                    );
                }
            }

            // detect block id within slab and push it to the free list
            bytes = SLAB_CLASS[nodes[s].size];  // update bytes to reflect actual block size
            uint16_t block = slab_block(s, p);
            bytes.clear(slab_data(s, block));  // zero memory for security purposes
            slab_push(s, block);
            occupied.fetch_sub(  // decrement occupied by 1 block
                bytes.value,
                std::memory_order_release
            );

            // if the slab was previously full, reinsert it into `slabs`
            if (nodes[s].heap == 1) {
                nodes[s].right = slabs[nodes[s].size];
                if (nodes[s].right != NIL) {
                    nodes[nodes[s].right].left = s;
                }
                nodes[s].left = NIL;
                slabs[nodes[s].size] = s;

            // if the slab is now empty and is not the only partial slab, reclaim it to
            // save memory
            } else if (
                nodes[s].heap == SLAB_BLOCKS[nodes[s].size] &&
                (nodes[s].left != NIL || nodes[s].right != NIL)
            ) {
                if (nodes[s].left != NIL) {
                    nodes[nodes[s].left].right = nodes[s].right;
                } else {
                    slabs[nodes[s].size] = nodes[s].right;
                }
                if (nodes[s].right != NIL) {
                    nodes[nodes[s].right].left = nodes[s].left;
                }
                nodes[s].right = NIL;
                nodes[s].left = NIL;
                occupied.fetch_add(  // don't count empty slab space
                    size_t(SLAB_PAGES) << PAGE_SHIFT,
                    std::memory_order_release
                );
                page_deallocate(nodes[s].offset, SLAB_PAGES);
                put_node(s);
            }
        }

        ///////////////////////////
        ////    REMOTE FREE    ////
        ///////////////////////////

        /* Push a remote free request for a slab deallocation onto this address space's
        lock-free stack.  This must be called from another thread after mapping the
        pointer to this address space, in order to properly deallocate memory
        originating from it. */
        void free_slab(void* p) {
            // if we are issuing a remote free after thread teardown, then just
            // modify `occupied` directly and leak the memory, since the final unmap
            // will clean it up anyways and it isn't safe to modify local data
            // structures remotely
            if (owner.load(std::memory_order_acquire) == nullptr) {
                occupied.fetch_sub(
                    SLAB_CLASS[nodes[slab(p)].size].value,
                    std::memory_order_release
                );

            // if the thread is still alive, then just push to its remote free stack
            // and wait for it to drain locally
            } else {
                defer_slab* req = std::construct_at(static_cast<defer_slab*>(p));
                defer_slab* old = remote_slab.load(std::memory_order_relaxed);
                do {
                    req->next = old;
                } while (!remote_slab.compare_exchange_weak(
                    old,
                    req,
                    std::memory_order_release,
                    std::memory_order_relaxed
                ));

                // if the owner shut down while we were pushing, then we need to
                // decrement occupied as above
                if (owner.load(std::memory_order_acquire) == nullptr) {
                    defer_slab* s = remote_slab.exchange(nullptr, std::memory_order_acquire);
                    while (s != nullptr) {
                        auto* next = s->next;
                        occupied.fetch_sub(
                            SLAB_CLASS[nodes[slab(s)].size].value,
                            std::memory_order_release
                        );
                        s = next;
                    }
                }
            }
        }

        /* Push a remote free request for a page deallocation onto this address space's
        lock-free stack.  This must be called from another thread after mapping the
        pointer to this address space, in order to properly deallocate memory
        originating from it. */
        void free_page(void* p, uint32_t pages) noexcept {
            // if we are issuing a remote free after thread teardown, then just
            // modify `occupied` directly and leak the memory, since the final unmap
            // will clean it up anyways and it isn't safe to modify local data
            // structures remotely
            if (owner.load(std::memory_order_acquire) == nullptr) {
                occupied.fetch_sub(
                    size_t(pages) << PAGE_SHIFT,
                    std::memory_order_release
                );

            // if the thread is still alive, then just push to its remote free stack
            // and wait for it to drain locally
            } else {
                defer_page* req = std::construct_at(static_cast<defer_page*>(p));
                req->pages = pages;
                defer_page* old = remote_page.load(std::memory_order_relaxed);
                do {
                    req->next = old;
                } while (!remote_page.compare_exchange_weak(
                    old,
                    req,
                    std::memory_order_release,
                    std::memory_order_relaxed
                ));

                // if the owner shut down while we were pushing, then we need to
                // decrement occupied as above
                if (owner.load(std::memory_order_acquire) == nullptr) {
                    defer_page* p = remote_page.exchange(nullptr, std::memory_order_acquire);
                    while (p != nullptr) {
                        auto* next = p->next;
                        occupied.fetch_sub(
                            size_t(p->pages) << PAGE_SHIFT,
                            std::memory_order_release
                        );
                        p = next;
                    }
                }
            }
        }

        /* Process all of the remote free requests, if there are any. */
        void drain_remote() noexcept {
            // drain slab deallocations
            defer_slab* s = remote_slab.exchange(nullptr, std::memory_order_acquire);
            while (s != nullptr) {
                auto* next = s->next;
                capacity<> bytes = 0;
                slab_deallocate(s, bytes);
                s = next;
            }

            // drain page deallocations
            defer_page* p = remote_page.exchange(nullptr, std::memory_order_acquire);
            while (p != nullptr) {
                auto* next = p->next;
                size_t offset = static_cast<size_t>(
                    reinterpret_cast<std::byte*>(p) - static_cast<std::byte*>(ptr)
                );
                if constexpr (VMEM_DEBUG) {
                    if ((offset & PAGE_MASK) != 0) {
                        throw MemoryError("page deallocation pointer is not page-aligned");
                    }
                }
                page_deallocate(
                    static_cast<uint32_t>(offset >> PAGE_SHIFT),
                    p->pages
                );
                p = next;
            }
        }
    };

    /* The TLS `address_space` object is actually just an RAII handle for a region
    of virtual memory beginning with a header that stores all the relevant
    metadata.  When the `address_space` is first accessed, the underlying memory
    will be lazily mapped and initialized, transitioning this pointer away from
    null to an address aligned to `VMEM_PER_THREAD`.  This alignment is exploited to
    quickly map an allocated pointer back to its parent address space during
    remote free operations, and placing the header at the start of the region
    allows for direct casting without any extra pointer indirection.
    
    When the `address_space` is destroyed (at thread exit), this pointer will be
    incremented by 1, resulting in an unaligned address.  This signals that the
    address space is in the shutdown phase, and no further allocations will be
    accepted, but existing allocations may still be freed.  Finally, when the last
    allocations is freed, the entire address space will be unmapped and returned
    to the OS, without leaving any orphaned memory. */
    inline constinit thread_local struct address_space_handle {
    private:
        template <meta::unqualified T, impl::capacity<T> N>
        friend struct bertrand::Space;

        address_space* data = nullptr;

        /* Map a pointer to its containing address space by exploiting alignment by
        `VMEM_PER_THREAD`, assuming the pointer originates from an address space. */
        [[nodiscard]] static address_space* find(void* p) noexcept {
            return reinterpret_cast<address_space*>(
                reinterpret_cast<uintptr_t>(p) & ~(VMEM_PER_THREAD.value - 1)
            );
        }

        /* Because slab allocations are always aligned to the system's native pointer
        width, the lowest 2-3 bits will always be clear, meaning we can use one of them
        to tag slab allocations without affecting user code, as long as we remember to
        always clear that bit when we need the actual address.  Note that only the
        outermost `allocate()`, `deallocate()`, and `reserve()` methods ever need to
        check for this, and all other helpers will instead receive a properly-aligned
        pointer.  `Space<T, N>::data()` will also strip this bit automatically so as
        not to interfere with normal usage. */
        static constexpr uintptr_t SLAB_MASK = uintptr_t(0b1);
        [[nodiscard]] static bool slab(void* p) noexcept {
            return (reinterpret_cast<uintptr_t>(p) & SLAB_MASK) != 0;
        }
        [[nodiscard]] static void* set_slab(void* p) noexcept {
            return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(p) | SLAB_MASK);
        }
        [[nodiscard]] static void* clear_slab(void* p) noexcept {
            return reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(p) & ~SLAB_MASK);
        }

        /* All allocator interactions begin by incrementing a quiescence counter in
        the address space's header, and end by decrementing it via an RAII guard.
        Allocations will also map the address space on first use if it is not already
        initialized, and deallocations (whether local or otherwise) will check to see
        if the address space can be unmapped during destruction.  This pattern ensures
        that the address space remains mapped until this handle's destructor has run,
        there are no concurrent requests touching its data, and we have just
        deallocated the last occupied region, regardless of initialization order. */
        struct acquire {
            address_space_handle* self;
            constexpr acquire(address_space_handle* self) : self(self) {
                if (self->data == nullptr) {
                    // map address space, aligning to `VMEM_PER_THREAD` in order to
                    // enable fast back-referencing from allocated pointers
                    self->data = static_cast<address_space*>(map_address_space(
                        address_space::SIZE.total(),
                        VMEM_PER_THREAD
                    ));

                    // commit base private partition size
                    try {
                        commit_address_space(self->data, address_space::SIZE.commit());
                    } catch (...) {
                        unmap_address_space(self->data, address_space::SIZE.total());
                        self->data = nullptr;
                        throw;
                    }

                    // initialize internal data structures
                    std::construct_at(self->data, self);
                }
                self->data->in_flight.fetch_add(1, std::memory_order_acquire);
            }
            constexpr ~acquire() noexcept {
                self->data->in_flight.fetch_sub(1, std::memory_order_release);
            }
        };
        struct release {
            address_space* data;
            constexpr release(address_space* data) noexcept : data(data) {
                data->in_flight.fetch_add(1, std::memory_order_acquire);
            }
            constexpr ~release() noexcept (false) {
                size_t active = data->in_flight.fetch_sub(1, std::memory_order_acq_rel);
                if (
                    active == 1 &&
                    data->owner.load(std::memory_order_acquire) == nullptr &&
                    data->occupied.load(std::memory_order_acquire) == 0
                ) {
                    unmap_address_space(data, address_space::SIZE.total());
                }
            }
        };

        /* Request `bytes` worth of memory from the address space, returning a pointer
        to the allocated region and updating `bytes` to the actual allocated size.  If
        `bytes <= SLAB_MAX`, then the allocation will be served from a slab allocator
        using pre-touched memory, where the block stride is guaranteed to be a multiple
        of `alignment`.  Otherwise, an uncommitted region of pages will be reserved
        from the address space and committed as needed.  This will be called in the
        constructor for `Space<T, N>` to acquire backing memory. */
        [[nodiscard]] void* allocate(capacity<>& bytes, capacity<> alignment) {
            acquire guard(this);
            if constexpr (VMEM_DEBUG) {
                if (data->owner.load(std::memory_order_acquire) == nullptr) {
                    throw MemoryError(
                        "attempted allocation from address space in teardown phase"
                    );
                }
            }

            // process any remote frees first
            data->drain_remote();

            // attempt slab allocation first and tag result
            if (bytes <= address_space::SLAB_MAX && alignment <= address_space::SLAB_MIN) {
                uint16_t size_class = address_space::slab_class(bytes, alignment);
                if (size_class < address_space::SLABS) {
                    bytes = address_space::SLAB_CLASS[size_class];
                    return set_slab(data->slab_allocate(size_class));
                }
            }

            // fall back to page allocation
            uint32_t pages = bytes.pages();
            bytes = size_t(pages) << PAGE_SHIFT;
            return
                static_cast<std::byte*>(data->ptr) +
                (size_t(data->page_allocate(pages, alignment)) << PAGE_SHIFT);
        }

        /* Release `bytes` worth of memory back to the address space, starting at the
        given pointer `p`.  If `bytes` is less than or equal to `SLAB_MAX`, then the
        pointer will be searched against the slab treap to find an enclosing slab.  If
        one exists, then the block will be returned to the slab's free list and the
        slab will remain alive until all blocks are freed.  Otherwise, the pointer must
        be aligned to a page boundary, in which case `bytes` will be rounded up to the
        nearest page and released back to the address space, updating the allocator's
        internal data structures accordingly and advising the OS to reclaim physical
        pages.  The OS hook will be invoked in batches, amortizing its cost.  This will
        be called in the destructor for `Space<T, N>` to recycle backing memory. */
        void deallocate(void* p, capacity<>& bytes) {
            if constexpr (VMEM_DEBUG) {
                if (data == nullptr) {
                    throw MemoryError(
                        "attempted deallocation from uninitialized address space"
                    );
                }
            }

            // local deallocation
            void* norm = clear_slab(p);
            if (contains(norm)) {
                release guard(data);
                data->drain_remote();  // process any remote frees first
                if (slab(p)) {
                    data->slab_deallocate(norm, bytes);
                } else {
                    size_t offset =
                        static_cast<std::byte*>(norm) - static_cast<std::byte*>(data->ptr);
                    if constexpr (VMEM_DEBUG) {
                        if ((offset & PAGE_MASK) != 0) {
                            throw MemoryError("page deallocation pointer is not page-aligned");
                        }
                    }
                    bytes = bytes.pages();
                    data->page_deallocate(
                        static_cast<uint32_t>(offset >> PAGE_SHIFT),
                        static_cast<uint32_t>(bytes.value)
                    );
                    bytes <<= PAGE_SHIFT;
                }

            // remote deallocation
            } else {
                address_space* d = find(norm);
                release guard(d);
                if (slab(p)) {
                    d->free_slab(norm);
                } else {
                    bytes = bytes.pages();
                    d->free_page(norm, static_cast<uint32_t>(bytes.value));
                    bytes <<= PAGE_SHIFT;
                }
            }
        }

        /* Extend an allocation to the right by at least the specified number of
        `bytes` (rounding up to the nearest page).  Returns the number of additional
        bytes that were allocated or zero if the allocation could not be extended
        in-place.  Note that `p` must point to the end of a region previously returned
        by `allocate()` or `reserve()`, and slab blocks will always return zero in
        order to force relocation to a larger slab or page allocation instead.
        Attempting to extend an allocation from another thread will also return zero,
        which may force relocation to the current thread as well.  This will be called
        in `Space<T, N>::reserve()`, and subsequently by any container methods or
        growth operations that call it in turn. */
        [[nodiscard]] capacity<> reserve(void* p, capacity<> bytes) {
            acquire guard(this);
            if constexpr (VMEM_DEBUG) {
                if (data->owner.load(std::memory_order_acquire) == nullptr) {
                    throw MemoryError(
                        "attempted reservation from address space in teardown phase"
                    );
                }
                if (slab(p)) {
                    throw MemoryError("attempted reservation on slab allocation");
                }
                if (!contains(p)) {
                    throw MemoryError(
                        "reserved pointer does not originate from this address space"
                    );
                }
            }

            // process any remote frees first
            data->drain_remote();

            // attempt to grow in-place to page boundary
            size_t offset = size_t(static_cast<std::byte*>(p) - static_cast<std::byte*>(data->ptr));
            if constexpr (VMEM_DEBUG) {
                if ((offset & PAGE_MASK) != 0) {
                    throw MemoryError("reserve() pointer is not page-aligned");
                }
            }
            return data->page_reserve(
                static_cast<uint32_t>(offset >> PAGE_SHIFT),
                bytes.pages()
            );
        }

    public:
        [[nodiscard]] constexpr address_space_handle() noexcept = default;
        constexpr address_space_handle(const address_space_handle&) = delete;
        constexpr address_space_handle(address_space_handle&&) = delete;
        constexpr address_space_handle& operator=(const address_space_handle&) = delete;
        constexpr address_space_handle& operator=(address_space_handle&&) = delete;
        ~address_space_handle() noexcept (false) {
            if (data != nullptr) {
                release guard(data);
                data->owner.store(nullptr, std::memory_order_release);  // signal teardown
                data->drain_remote();  // process any remaining remote frees
                data = nullptr;  // if reawakened later, remap a new address space
            }
        }

        /* The size of the local address space reflects the current amount of virtual
        memory that has been allocated from it in bytes (which is not necessarily equal
        to the amount of physical ram being consumed). */
        [[nodiscard]] constexpr size_t size() const noexcept {
            return data == nullptr ? 0 : data->occupied.load(std::memory_order_acquire);
        }
        [[nodiscard]] constexpr ssize_t ssize() const noexcept { return ssize_t(size()); }
        [[nodiscard]] constexpr bool empty() const noexcept {
            return data == nullptr || data->occupied.load(std::memory_order_acquire) == 0;
        }

        /* Check whether an arbitrary pointer maps to a region within this address
        space. */
        [[nodiscard]] constexpr bool contains(void* p) const noexcept {
            if (data == nullptr) return false;
            uintptr_t x = reinterpret_cast<uintptr_t>(p);
            uintptr_t y = reinterpret_cast<uintptr_t>(data);
            return x >= y && x < y + address_space::SIZE.total();
        }

        /* Map a pointer to its originating address space, assuming it was allocated
        from one.  May result in undefined behavior if `p` is not associated with any
        address space, and may return null if the originating address space is
        currently in the shutdown process. */
        [[nodiscard]] address_space_handle* origin(void* p) noexcept {
            return find(p)->owner.load(std::memory_order_acquire);
        }
    } local_address_space {};

}


/* A special case of `Space<T, N>` for dynamic (heap or virtual memory-based) spaces,
where `N` is not known until run time.  See the primary template for full more
details. */
template <meta::unqualified T, impl::capacity<T> N> requires (N == 0)
struct Space<T, N> {
    using type = T;

    /* Identifies static (stack-based) vs dynamic (heap or virtual memory-based)
    spaces.  If true, then the space may be grown, shrunk, or moved, but not copied. */
    [[nodiscard]] static constexpr bool dynamic() noexcept { return true; }

    /* Differentiates heap-based and virtual memory-based spaces.  These have identical
    interfaces, but may exhibit different performance characteristics subject to this
    class's documentation. */
    [[nodiscard]] constexpr bool heap() const noexcept {
        if consteval {
            return true;
        } else {
            return VMEM_PER_THREAD == 0;
        }
    }

    /* Detect whether a virtual memory-based space is backed by a slab allocator or
    page allocations.  Slab allocations are used for small sizes to reduce memory
    overhead and TLB pressure, but cannot be arbitrarily grown or shrunk in-place like
    page allocations can.  Normally, this is not something the user needs to be
    concerned about, but it may be useful for debugging or performance analysis.  Note
    that it is possible to avoid slab allocations entirely by providing an alignment
    requirement greater than a single pointer during construction. */
    [[nodiscard]] constexpr bool slab() const noexcept {
        if consteval {
            return false;
        } else {
            if constexpr (VMEM_PER_THREAD == 0) {
                return false;
            } else {
                return impl::local_address_space.slab(m_ptr);
            }
        }
    }

    /* Returns true if the space owns at least one element. */
    [[nodiscard]] constexpr explicit operator bool() const noexcept { return m_bytes > 0; }

    /* Returns true if the space is empty, meaning it does not own any memory. */
    [[nodiscard]] constexpr bool empty() const noexcept { return m_bytes == 0; }

    /* Returns the number of elements in the space as an unsigned integer. */
    [[nodiscard]] constexpr size_t size() const noexcept { return m_bytes.value / N.aligned_size; }

    /* Returns the number of elements in the space as a signed integer. */
    [[nodiscard]] constexpr ssize_t ssize() const noexcept { return ssize_t(size()); }

    /* Returns the space's current capacity in bytes as an `impl::capacity<void>`
    object.  Such capacities can be explicitly converted to `size_t`, dereferenced, or
    accessed via `.value` or `.bytes()`, all of which do the same thing.  Converting
    the result to another non-void capacity will maintain proper alignment for the
    target type, changing units but not the overall memory footprint. */
    [[nodiscard]] constexpr impl::capacity<> capacity() const noexcept { return m_bytes; }

    void* m_ptr = nullptr;
    impl::capacity<> m_bytes = 0;

    /* Default-constructing a dynamic space does not allocate any memory and sets the
    capacity to zero. */
    [[nodiscard]] constexpr Space() noexcept = default;

    /* Passing a size as a constructor argument will dynamically allocate at least that
    many elements of type `T` from either the heap or a virtual memory.  If the size is
    zero, then no allocation will be performed.  Explicit units may be used to specify
    a size in bytes instead of number of elements, in which case it will be rounded
    to the nearest multiple of `sizeof(T)`.  A `MemoryError` may be thrown if the
    allocation fails. */
    [[nodiscard]] constexpr Space(impl::capacity<type> size) : m_bytes(size.bytes()) {
        if (m_bytes > 0) {
            if consteval {
                m_ptr = std::allocator<type>{}.allocate(this->size());
                if (m_ptr == nullptr) {
                    throw MemoryError("failed to allocate memory for Space");
                }
            } else {
                if constexpr (VMEM_PER_THREAD == 0) {
                    m_ptr = std::allocator<type>{}.allocate(this->size());
                    if (m_ptr == nullptr) {
                        throw MemoryError("failed to allocate memory for Space");
                    }
                } else {
                    m_ptr = impl::local_address_space.allocate(
                        m_bytes,
                        alignof(type)  // always a power of 2
                    );
                }
            }
        }
    }

    /* Spaces are never copyable. */
    [[nodiscard]] constexpr Space(const Space&) = delete;
    constexpr Space& operator=(const Space&) = delete;

    /* Dynamic spaces are movable, transferring ownership of the underlying memory. */
    [[nodiscard]] constexpr Space(Space&& other) noexcept :
        m_ptr(other.m_ptr),
        m_bytes(other.m_bytes)
    {
        other.m_ptr = nullptr;
        other.m_bytes = 0;
    }
    constexpr Space& operator=(Space&& other) noexcept {
        if (this != &other) {
            if (m_ptr != nullptr) {
                if consteval {
                    std::allocator<type>{}.deallocate(static_cast<type*>(m_ptr), size());
                } else {
                    if constexpr (VMEM_PER_THREAD == 0) {
                        std::allocator<type>{}.deallocate(static_cast<type*>(m_ptr), size());
                    } else {
                        impl::local_address_space.deallocate(m_ptr, m_bytes);
                    }
                }
            }
            m_ptr = other.m_ptr;
            m_bytes = other.m_bytes;
            other.m_ptr = nullptr;
            other.m_bytes = 0;
        }
        return *this;
    }

    /* Destroying a dynamic space deallocates the underlying memory according to
    RAII. */
    constexpr ~Space() noexcept {
        if (m_ptr != nullptr) {
            if consteval {
                std::allocator<type>{}.deallocate(static_cast<type*>(m_ptr), size());
            } else {
                if constexpr (VMEM_PER_THREAD == 0) {
                    std::allocator<type>{}.deallocate(static_cast<type*>(m_ptr), size());
                } else {
                    impl::local_address_space.deallocate(m_ptr, m_bytes);
                }
            }
            m_ptr = nullptr;
            m_bytes = 0;
        }
    }

    /* Spaces can be used as pointers similar to C-style arrays. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) operator*(this Self&& self) noexcept {
        return std::forward<Self>(self).front();
    }
    [[nodiscard]] constexpr T* operator->() noexcept { return data(); }
    [[nodiscard]] constexpr const T* operator->() const noexcept { return data(); }

    /* Retrieve a pointer to a specific element identified by index or memory offset,
    defaulting to the first value in the space.  Similar to `construct()` and
    `destroy()`, an alternate type `V` may be specified as a template parameter, in
    which case the returned pointer will be reinterpreted as that type.  Note that the
    returned pointer will always be properly aligned for type `T`, and it is up to the
    user to ensure that neither this nor the reinterpretation leads to undefined
    behavior. */
    template <meta::unqualified V = type>
    [[nodiscard]] constexpr V* data(impl::capacity<type> i = 0) noexcept {
        using from = std::conditional_t<meta::is_void<type>, std::byte, type>;
        if consteval {
            if constexpr (meta::explicitly_convertible_to<from*, V*>) {
                return static_cast<V*>(static_cast<from*>(m_ptr) + i.value);
            } else {
                return reinterpret_cast<V*>(static_cast<from*>(m_ptr) + i.value);
            }
        } else {
            if constexpr (VMEM_PER_THREAD == 0) {
                if constexpr (meta::explicitly_convertible_to<from*, V*>) {
                    return static_cast<V*>(static_cast<from*>(m_ptr) + i.value);
                } else {
                    return reinterpret_cast<V*>(static_cast<from*>(m_ptr) + i.value);
                }
            } else {
                if constexpr (meta::explicitly_convertible_to<from*, V*>) {
                    return static_cast<V*>(static_cast<from*>(
                        impl::local_address_space.clear_slab(m_ptr)
                    ) + i.value);
                } else {
                    return reinterpret_cast<V*>(static_cast<from*>(
                        impl::local_address_space.clear_slab(m_ptr)
                    ) + i.value);
                }
            }
        }
    }
    template <meta::unqualified V = type>
    [[nodiscard]] constexpr const V* data(impl::capacity<type> i = 0) const noexcept {
        using from = std::conditional_t<meta::is_void<type>, std::byte, type>;
        if consteval {
            if constexpr (meta::explicitly_convertible_to<const from*, const V*>) {
                return static_cast<const V*>(static_cast<const from*>(m_ptr) + i.value);
            } else {
                return reinterpret_cast<const V*>(static_cast<const from*>(m_ptr) + i.value);
            }
        } else {
            if constexpr (VMEM_PER_THREAD == 0) {
                if constexpr (meta::explicitly_convertible_to<const from*, const V*>) {
                    return static_cast<const V*>(static_cast<const from*>(m_ptr) + i.value);
                } else {
                    return reinterpret_cast<const V*>(static_cast<const from*>(m_ptr) + i.value);
                }
            } else {
                if constexpr (meta::explicitly_convertible_to<const from*, const V*>) {
                    return static_cast<const V*>(static_cast<const from*>(
                        impl::local_address_space.clear_slab(m_ptr)
                    ) + i.value);
                } else {
                    return reinterpret_cast<const V*>(static_cast<const from*>(
                        impl::local_address_space.clear_slab(m_ptr)
                    ) + i.value);
                }
            }
        }
    }

    /* Perfectly forward the first element of the space. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) front(this Self&& self) noexcept {
        if constexpr (meta::lvalue<Self>) {
            return (*self.data());
        } else {
            return (std::move(*self.data()));
        }
    }

    /* Perfectly forward the last element of the space. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) back(this Self&& self) noexcept {
        if constexpr (meta::lvalue<Self>) {
            return (*(self.data(self.m_bytes) - 1));
        } else {
            return (std::move(*(self.data(self.m_bytes) - 1)));
        }
    }

    /* Perfectly forward an element of the space identified by index. */
    template <typename Self>
    [[nodiscard]] constexpr decltype(auto) operator[](
        this Self&& self,
        impl::capacity<type> i
    ) noexcept {
        if constexpr (meta::lvalue<Self>) {
            return (*self.data(i));
        } else {
            return (std::move(*self.data(i)));
        }
    }

    /* Get a forward iterator over the space. */
    template <typename Self>
    [[nodiscard]] constexpr auto begin(this Self&& self) noexcept {
        if constexpr (meta::lvalue<Self>) {
            return self.data();
        } else {
            return std::move_iterator(self.data());
        }
    }

    /* Get a forward sentinel for the space. */
    template <typename Self>
    [[nodiscard]] constexpr auto end(this Self&& self) noexcept {
        if constexpr (meta::lvalue<Self>) {
            return self.data(self.m_bytes);
        } else {
            return std::move_iterator(self.data(self.m_bytes));
        }
    }

    /* Get a reverse iterator over the space. */
    template <typename Self>
    [[nodiscard]] constexpr auto rbegin(this Self&& self) noexcept {
        return std::make_reverse_iterator(std::forward<Self>(self).end());
    }

    /* Get a reverse sentinel for the space. */
    template <typename Self>
    [[nodiscard]] constexpr auto rend(this Self&& self) noexcept {
        return std::make_reverse_iterator(std::forward<Self>(self).begin());
    }

    /* Construct the element at the specified index of the space using the remaining
    arguments and return a reference to the newly constructed instance.  A different
    type may be provided as a template parameter, in which case the element will be
    reinterpreted as that type before constructing it.  The type may also be provided
    as a template, in which case CTAD will be applied to deduce the actual type to
    construct.  Note that it is up to the user to ensure that this does not lead to
    undefined behavior. */
    template <meta::unqualified V = type, typename... A>
    constexpr decltype(auto) construct(impl::capacity<type> i, A&&... args) &
        noexcept (meta::nothrow::constructible_from<V, A...>)
        requires (meta::constructible_from<V, A...>)
    {
        return (*std::construct_at(data<V>(i), std::forward<A>(args)...));
    }
    template <meta::unqualified V = type, typename... A>
    constexpr decltype(auto) construct(impl::capacity<type> i, A&&... args) &&
        noexcept (meta::nothrow::constructible_from<V, A...>)
        requires (meta::constructible_from<V, A...>)
    {
        return (std::move(*std::construct_at(data<V>(i), std::forward<A>(args)...)));
    }
    template <template <typename...> typename V, typename... A>
    constexpr decltype(auto) construct(impl::capacity<type> i, A&&... args) &
        noexcept (requires{{V{std::forward<A>(args)...}} noexcept;})
        requires (requires{{V{std::forward<A>(args)...}};})
    {
        using to = decltype(V{std::forward<A>(args)...});
        return (*std::construct_at(data<to>(i), std::forward<A>(args)...));
    }
    template <template <typename...> typename V, typename... A>
    constexpr decltype(auto) construct(impl::capacity<type> i, A&&... args) &&
        noexcept (requires{{V{std::forward<A>(args)...}} noexcept;})
        requires (requires{{V{std::forward<A>(args)...}};})
    {
        using to = decltype(V{std::forward<A>(args)...});
        return (std::move(*std::construct_at(data<to>(i), std::forward<A>(args)...)));
    }

    /* Destroy the element at the specified index of the space. */
    template <meta::unqualified V = type>
    constexpr void destroy(impl::capacity<type> i)
        noexcept (meta::nothrow::destructible<V>)
        requires (meta::destructible<V>)
    {
        std::destroy_at(data<V>(i));
    }

    /* Attempt to reserve additional memory for the space without relocating existing
    elements.  The `size` argument indicates the desired minimum capacity of the space
    after growth.  If the current capacity is greater than or equal to the requested
    size, or if the space does not support in-place resizing, or originates from
    another thread, then no action is taken.  These cases can be detected by checking
    the capacity before and after calling this method, and handled by relocating its
    contents to another space if necessary.  Note that no constructors will be called
    for the new elements, so users must ensure that lifetimes are properly managed
    after calling this method. */
    constexpr void reserve(impl::capacity<type> size) {
        if !consteval {
            if constexpr (VMEM_PER_THREAD > 0) {
                if (
                    !slab() &&
                    impl::local_address_space.contains(m_ptr) &&
                    size.bytes() > m_bytes
                ) {
                    m_bytes += impl::local_address_space.reserve(
                        static_cast<std::byte*>(
                            impl::local_address_space.clear_slab(m_ptr)
                        ) + m_bytes.value,
                        size.bytes() - m_bytes.value
                    );
                }
            }
        }
    }

    /* Attempt to partially release memory back to the originating allocator, shrinking
    the space to the requested `size`.  If the current capacity is less than or equal
    to the requested size, or if the space does not support in-place resizing, or
    originates from another thread, then no action is taken.  These cases can be
    detected by checking the capacity before and after calling this method, and handled
    by relocating its contents to another space if necessary.  Note that no destructors
    will be called for the truncated elements, so the user must ensure that lifetimes
    are properly managed before calling this method. */
    constexpr void truncate(impl::capacity<type> size) {
        if !consteval {
            if constexpr (VMEM_PER_THREAD > 0) {
                impl::capacity<> bytes = size.pages() << PAGE_SHIFT;
                if (
                    !slab() &&
                    impl::local_address_space.contains(m_ptr) &&
                    bytes < m_bytes
                ) {
                    bytes = m_bytes - bytes;
                    impl::local_address_space.deallocate(
                        static_cast<std::byte*>(
                            impl::local_address_space.clear_slab(m_ptr)
                        ) + m_bytes.value,
                        bytes  // updated in-place
                    );
                    m_bytes -= bytes;
                }
            }
        }
    }
};


}


#endif  // BERTRAND_ALLOCATE_H
