#ifndef BERTRAND_ITER_SEQUENCE_H
#define BERTRAND_ITER_SEQUENCE_H

#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/iter/range.h"


namespace bertrand {


namespace impl {

    /// TODO: update and streamline error messages.  This might not be doable until
    /// after strings are defined, since otherwise I get undefined template errors.

    template <meta::not_void T> requires (meta::not_rvalue<T>)
    struct sequence_iterator;

    namespace sequence_dtor {
        using ptr = void(*)(void*);

        template <typename C>
        constexpr void _fn(void* ptr) {
            if constexpr (!meta::lvalue<C> || !meta::has_address<C>) {
                delete reinterpret_cast<meta::as_pointer<C>>(ptr);
            }
        }

        template <typename C>
        constexpr ptr fn = &_fn<C>;
    }

    namespace sequence_size {
        using ptr = size_t(*)(void*);
        using err = TypeError(*)();

        template <meta::const_ref C> requires (meta::has_size<C>)
        constexpr size_t _fn(void* ptr) {
            return std::ranges::size(*reinterpret_cast<meta::as_pointer<C>>(ptr));
        }

        template <meta::const_ref C> requires (DEBUG && !meta::has_size<C>)
        constexpr TypeError _error() {
            /// TODO: provide the demangled type name in the error message for clarity.
            return TypeError("underlying container does not support size()");
        }

        template <meta::const_ref C>
        constexpr ptr fn = nullptr;
        template <meta::const_ref C> requires (meta::has_size<C>)
        constexpr ptr fn<C> = &_fn<C>;

        template <meta::const_ref C>
        constexpr err error = nullptr;
        template <meta::const_ref C> requires (DEBUG && !meta::has_size<C>)
        constexpr err error<C> = &_error<C>;
    }

    namespace sequence_empty {
        using ptr = bool(*)(void*);
        using err = TypeError(*)();

        template <meta::const_ref C> requires (meta::has_empty<C>)
        constexpr bool _fn(void* ptr) {
            return std::ranges::empty(*reinterpret_cast<meta::as_pointer<C>>(ptr));
        }

        template <meta::const_ref C> requires (DEBUG && !meta::has_empty<C>)
        constexpr TypeError _error() {
            /// TODO: provide the demangled type name in the error message for clarity.
            return TypeError("underlying container does not support empty()");
        }

        template <meta::const_ref C>
        constexpr ptr fn = nullptr;
        template <meta::const_ref C> requires (meta::has_empty<C>)
        constexpr ptr fn<C> = &_fn<C>;

        template <meta::const_ref C>
        constexpr err error = nullptr;
        template <meta::const_ref C> requires (DEBUG && !meta::has_empty<C>)
        constexpr err error<C> = &_error<C>;
    }

    namespace sequence_subscript {
        template <meta::not_void T> requires (meta::not_rvalue<T>)
        using ptr = T(*)(void*, size_t);
        template <meta::not_void T> requires (meta::not_rvalue<T>)
        using err = TypeError(*)();

        template <meta::lvalue C, meta::not_void T>
            requires (meta::not_rvalue<T> && meta::index_returns<T, C, size_t>)
        constexpr T _fn(void* ptr, size_t i) {
            return (*reinterpret_cast<meta::as_pointer<C>>(ptr))[i];
        }

        template <meta::lvalue C, meta::not_void T>
            requires (meta::not_rvalue<T> && DEBUG && !meta::index_returns<T, C, size_t>)
        constexpr TypeError _error() {
            /// TODO: provide the demangled type name in the error message for clarity.
            return TypeError("underlying container does not support indexing");
        }

        template <meta::lvalue C, meta::not_void T>
        constexpr ptr<T> fn = nullptr;
        template <meta::lvalue C, meta::not_void T>
            requires (meta::not_rvalue<T> && meta::index_returns<T, C, size_t>)
        constexpr ptr<T> fn<C, T> = &_fn<C, T>;

        template <meta::lvalue C, meta::not_void T>
        constexpr err<T> error = nullptr;
        template <meta::lvalue C, meta::not_void T>
            requires (meta::not_rvalue<T> && DEBUG && !meta::index_returns<T, C, size_t>)
        constexpr err<T> error<C, T> = &_error<C, T>;
    }

    namespace sequence_begin {
        template <meta::not_void T> requires (meta::not_rvalue<T>)
        using ptr = sequence_iterator<T>(*)(void*);

        template <meta::lvalue C, meta::not_void T>
            requires (meta::not_rvalue<T> && meta::yields<C, T>)
        constexpr sequence_iterator<T> _fn(void* ptr) {
            return {
                reinterpret_cast<meta::as_pointer<C>>(ptr)->begin(),
                reinterpret_cast<meta::as_pointer<C>>(ptr)->end()
            };
        }

        template <meta::lvalue C, meta::not_void T>
            requires (meta::not_rvalue<T> && meta::yields<C, T>)
        constexpr ptr<T> fn = &_fn<C, T>;
    }

    /* Sequences are reference counted in order to allow for efficient copy/move
    semantics.  The only difference from a typical `shared_ptr` is that lvalue
    initializers will not be copied onto the heap, and will instead simply reference
    their current location as an optimization.  Copying large containers is potentially
    expensive, and should be done explicitly by manually moving or copying the
    value.

    This class represents the control block for the sequence, which stores all the
    function pointers needed to emulate the standard range interface, so that they
    don't have to be stored on the sequence itself.  A block of this form will always
    be heap allocated to back the underlying container when it is first converted into
    a sequence. */
    template <meta::not_void T> requires (meta::not_rvalue<T>)
    struct sequence_control {
        std::atomic<size_t> count = 0;
        const sequence_dtor::ptr dtor = nullptr;
        const sequence_size::ptr size_fn = nullptr;
        const sequence_empty::ptr empty_fn = nullptr;
        const sequence_subscript::ptr<T> subscript_fn = nullptr;
        const sequence_begin::ptr<T> begin_fn = nullptr;
    };

    /* When compiled in debug mode, the control block stores extra function pointers
    that throw detailed errors if `size()`, `empty()`, or `operator[]` are used when
    they are not available. */
    template <meta::not_void T> requires (meta::not_rvalue<T> && DEBUG)
    struct sequence_control<T> {
        std::atomic<size_t> count = 0;
        const sequence_dtor::ptr dtor = nullptr;
        const sequence_size::ptr size_fn = nullptr;
        const sequence_size::err size_err = nullptr;
        const sequence_empty::ptr empty_fn = nullptr;
        const sequence_empty::err empty_err = nullptr;
        const sequence_subscript::ptr<T> subscript_fn = nullptr;
        const sequence_subscript::err<T> subscript_err = nullptr;
        const sequence_begin::ptr<T> begin_fn = nullptr;
    };

    /* The underlying container type for a sequence serves as the public-facing
    component of its shared pointer-like memory model, which updates reference counts
    whenever it is copied or destroyed.  Note that because the underlying container
    type is erased, there's no way to tell at compile time whether it supports size,
    empty, or subscript access, so additional boolean properties are exposed to check
    for these, which must be used to avoid undefined behavior. */
    template <meta::not_void T> requires (meta::not_rvalue<T>)
    struct sequence {
        using type = T;
        using size_type = size_t;
        using index_type = ssize_t;

    private:
        void* data = nullptr;
        sequence_control<T>* control = nullptr;

        constexpr void decref() {
            if (control && control->count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::atomic_thread_fence(std::memory_order_acquire);
                control->dtor(data);
                delete control;
            }
        }

    public:
        [[nodiscard]] constexpr sequence() = default;

        template <meta::yields<T> C> requires (meta::lvalue<C> && meta::has_address<C>)
        [[nodiscard]] constexpr sequence(C&& c) noexcept (meta::nothrow::has_address<C>) :
            data(const_cast<meta::as_pointer<meta::remove_const<C>>>(std::addressof(c))),
            control(new sequence_control<T>{
                .count = 1,
                .dtor = sequence_dtor::fn<C>,  // no-op for lvalue references
                .size_fn = sequence_size::fn<meta::as_const_ref<C>>,
                .empty_fn = sequence_empty::fn<meta::as_const_ref<C>>,
                .subscript_fn = sequence_subscript::fn<meta::as_lvalue<C>, T>,
                .begin_fn = sequence_begin::fn<meta::as_lvalue<C>, T>
            })
        {}

        template <meta::yields<T> C> requires (!meta::lvalue<C> || !meta::has_address<C>)
        [[nodiscard]] constexpr sequence(C&& c) :
            data(new meta::unqualify<C>(std::forward<C>(c))),
            control(new sequence_control<T>{
                .count = 1,
                .dtor = sequence_dtor::fn<C>,  // calls `delete` on `data`
                .size_fn = sequence_size::fn<meta::as_const_ref<C>>,
                .empty_fn = sequence_empty::fn<meta::as_const_ref<C>>,
                .subscript_fn = sequence_subscript::fn<meta::as_lvalue<C>, T>,
                .begin_fn = sequence_begin::fn<meta::as_lvalue<C>, T>
            })
        {}

        [[nodiscard]] constexpr sequence(const sequence& other) noexcept :
            data(other.data),
            control(other.control)
        {
            if (control) {
                control->count.fetch_add(1, std::memory_order_relaxed);
            }
        }

        [[nodiscard]] constexpr sequence(sequence&& other) noexcept :
            data(other.data),
            control(other.control)
        {
            other.data = nullptr;
            other.control = nullptr;
        }

        constexpr sequence& operator=(const sequence& other) {
            if (this != &other) {
                decref();
                data = other.data;
                control = other.control;
                if (control) {
                    control->count.fetch_add(1, std::memory_order_relaxed);
                }
            }
            return *this;
        }

        constexpr sequence& operator=(sequence&& other) {
            if (this != &other) {
                decref();
                data = other.data;
                control = other.control;
                other.data = nullptr;
                other.control = nullptr;
            }
            return *this;
        }

        constexpr ~sequence() {
            decref();
            data = nullptr;
            control = nullptr;
        }

        constexpr void swap(sequence& other) noexcept {
            std::swap(data, other.data);
            std::swap(control, other.control);
        }

        [[nodiscard]] constexpr bool has_size() const noexcept {
            return control->size_fn != nullptr;
        }

        [[nodiscard]] constexpr size_type size() const {
            return control->size_fn(data);
        }

        [[nodiscard]] constexpr index_type ssize() const {
            return index_type(control->size_fn(data));
        }

        [[nodiscard]] constexpr bool has_empty() const noexcept {
            return control->empty_fn != nullptr;
        }

        [[nodiscard]] constexpr bool empty() const {
            return control->empty_fn(data);
        }

        [[nodiscard]] constexpr bool has_subscript() const noexcept {
            return control->subscript_fn != nullptr;
        }

        [[nodiscard]] constexpr T operator[](index_type i) const {
            if (has_size() && i < 0) {
                i += index_type(control->size_fn(data));
            }
            return control->subscript_fn(data, size_type(i));
        }

        [[nodiscard]] constexpr sequence_iterator<T> begin() const {
            return control->begin_fn(data);
        }

        [[nodiscard]] static constexpr NoneType end() noexcept { return {}; }
    };

    /* When compiled in debug mode, the sequence inserts extra error paths for when the
    size, empty, or subscript operators are applied to a container which does not
    support them, using the extra function pointers in the control block. */
    template <meta::not_void T> requires (meta::not_rvalue<T> && DEBUG)
    struct sequence<T> {
        using type = T;
        using size_type = size_t;
        using index_type = ssize_t;

    private:
        void* data = nullptr;
        sequence_control<T>* control = nullptr;

        constexpr void decref() {
            if (control && control->count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::atomic_thread_fence(std::memory_order_acquire);
                control->dtor(data);
                delete control;
            }
        }

    public:
        [[nodiscard]] constexpr sequence() = default;

        template <meta::yields<T> C> requires (meta::lvalue<C> && meta::has_address<C>)
        [[nodiscard]] constexpr sequence(C&& c) noexcept (meta::nothrow::has_address<C>) :
            data(const_cast<meta::as_pointer<meta::remove_const<C>>>(std::addressof(c))),
            control(new sequence_control<T>{
                .count = 1,
                .dtor = sequence_dtor::fn<C>,  // no-op for lvalue references
                .size_fn = sequence_size::fn<meta::as_const_ref<C>>,
                .size_err = sequence_size::error<meta::as_const_ref<C>>,
                .empty_fn = sequence_empty::fn<meta::as_const_ref<C>>,
                .empty_err = sequence_empty::error<meta::as_const_ref<C>>,
                .subscript_fn = sequence_subscript::fn<meta::as_lvalue<C>, T>,
                .subscript_err = sequence_subscript::error<meta::as_lvalue<C>, T>,
                .begin_fn = sequence_begin::fn<meta::as_lvalue<C>, T>
            })
        {}

        template <meta::yields<T> C> requires (!meta::lvalue<C> || !meta::has_address<C>)
        [[nodiscard]] constexpr sequence(C&& c) :
            data(new meta::unqualify<C>(std::forward<C>(c))),
            control(new sequence_control<T>{
                .count = 1,
                .dtor = sequence_dtor::fn<C>,  // calls `delete` on `data`
                .size_fn = sequence_size::fn<meta::as_const_ref<C>>,
                .size_err = sequence_size::error<meta::as_const_ref<C>>,
                .empty_fn = sequence_empty::fn<meta::as_const_ref<C>>,
                .empty_err = sequence_empty::error<meta::as_const_ref<C>>,
                .subscript_fn = sequence_subscript::fn<meta::as_lvalue<C>, T>,
                .subscript_err = sequence_subscript::error<meta::as_lvalue<C>, T>,
                .begin_fn = sequence_begin::fn<meta::as_lvalue<C>, T>
            })
        {}


        [[nodiscard]] constexpr sequence(const sequence& other) noexcept :
            data(other.data),
            control(other.control)
        {
            if (control) {
                control->count.fetch_add(1, std::memory_order_relaxed);
            }
        }

        [[nodiscard]] constexpr sequence(sequence&& other) noexcept :
            data(other.data),
            control(other.control)
        {
            other.data = nullptr;
            other.control = nullptr;
        }

        constexpr sequence& operator=(const sequence& other) {
            if (this != &other) {
                decref();
                data = other.data;
                control = other.control;
                if (control) {
                    control->count.fetch_add(1, std::memory_order_relaxed);
                }
            }
            return *this;
        }

        constexpr sequence& operator=(sequence&& other) {
            if (this != &other) {
                decref();
                data = other.data;
                control = other.control;
                other.data = nullptr;
                other.control = nullptr;
            }
            return *this;
        }

        constexpr ~sequence() {
            decref();
            data = nullptr;
            control = nullptr;
        }

        constexpr void swap(sequence& other) noexcept {
            std::swap(data, other.data);
            std::swap(control, other.control);
        }

        [[nodiscard]] constexpr bool has_size() const noexcept {
            return control->size_fn != nullptr;
        }

        [[nodiscard]] constexpr size_type size() const {
            if (control->size_err != nullptr) {
                throw control->size_err();
            }
            return control->size_fn(data);
        }

        [[nodiscard]] constexpr index_type ssize() const {
            if (control->size_err != nullptr) {
                throw control->size_err();
            }
            return index_type(control->size_fn(data));
        }

        [[nodiscard]] constexpr bool has_empty() const noexcept {
            return control->empty_fn != nullptr;
        }

        [[nodiscard]] constexpr bool empty() const {
            if (control->empty_err != nullptr) {
                throw control->empty_err();
            }
            return control->empty_fn(data);
        }

        [[nodiscard]] constexpr bool has_subscript() const noexcept {
            return control->subscript_fn != nullptr;
        }

        [[nodiscard]] constexpr T operator[](index_type i) const {
            index_type n = i;

            if (has_size()) {
                index_type size = index_type(control->size_fn(data));
                n += size * (n < 0);
                if (n < 0 || n >= size) {
                    /// TODO: fix this error message
                    throw IndexError(
                        "index " + std::to_string(i) +
                        " out of range for sequence of size " + std::to_string(size)
                    );
                }

            } else if (n < 0) {
                throw IndexError(
                    "negative index " + std::to_string(i) + " not allowed for sequence"
                );
            }

            return control->subscript_fn(data, size_type(n));
        }

        [[nodiscard]] constexpr sequence_iterator<T> begin() const {
            return control->begin_fn(data);
        }

        [[nodiscard]] static constexpr NoneType end() noexcept { return {}; }
    };

    /* Iterators also have to be type-erased and stored as void pointers.  Reference
    counting is not allowed in this case, however, and copying the iterator means
    allocating a new iterator to store the copy.  The only reason we don't use
    `std::unique_ptr` here is that it doesn't generally play well with void pointers,
    so it's just easier to implement our own. */
    struct sequence_iter_storage {
        using copy = void*(*)(void*);
        using destroy = void(*)(void*);

        template <typename T>
        static constexpr void* ctor(T&& obj) {
            return new meta::unqualify<T>(std::forward<T>(obj));
        }

        template <meta::not_reference T>
        static constexpr void* _copy(void* ptr) {
            return new meta::unqualify<T>(*reinterpret_cast<meta::as_pointer<T>>(ptr));
        }

        template <meta::not_reference T>
        static constexpr void dtor(void* ptr) {
            delete reinterpret_cast<meta::as_pointer<T>>(ptr);
        }

        void* begin = nullptr;
        copy begin_copy = nullptr;
        destroy begin_dtor = nullptr;

        void* end = nullptr;
        copy end_copy = nullptr;
        destroy end_dtor = nullptr;

        [[nodiscard]] constexpr sequence_iter_storage() noexcept = default;

        template <typename Begin, typename End>
        [[nodiscard]] constexpr sequence_iter_storage(Begin&& b, End&& e) :
            begin(ctor(std::forward<Begin>(b))),
            begin_copy(&_copy<meta::remove_reference<Begin>>),
            begin_dtor(&dtor<meta::remove_reference<Begin>>),
            end(ctor(std::forward<End>(e))),
            end_copy(&_copy<meta::remove_reference<End>>),
            end_dtor(&dtor<meta::remove_reference<End>>)
        {}

        [[nodiscard]] constexpr sequence_iter_storage(const sequence_iter_storage& other) :
            begin(other.begin ? other.begin_copy(other.begin) : nullptr),
            begin_copy(other.begin_copy),
            begin_dtor(other.begin_dtor),
            end(other.end ? other.end_copy(other.end) : nullptr),
            end_copy(other.end_copy),
            end_dtor(other.end_dtor)
        {}

        [[nodiscard]] constexpr sequence_iter_storage(sequence_iter_storage&& other) noexcept :
            begin(other.begin),
            begin_copy(other.begin_copy),
            begin_dtor(other.begin_dtor),
            end(other.end),
            end_copy(other.end_copy),
            end_dtor(other.end_dtor)
        {
            other.begin = nullptr;
            other.end = nullptr;
        }

        constexpr sequence_iter_storage& operator=(const sequence_iter_storage& other) {
            if (this != &other) {
                if (begin) begin_dtor(begin);
                if (end) end_dtor(end);
                begin = other.begin ? other.begin_copy(other.begin) : nullptr;
                begin_copy = other.begin_copy;
                begin_dtor = other.begin_dtor;
                end = other.end ? other.end_copy(other.end) : nullptr;
                end_copy = other.end_copy;
                end_dtor = other.end_dtor;
            }
            return *this;
        }

        constexpr sequence_iter_storage& operator=(sequence_iter_storage& other) {
            if (this != &other) {
                if (begin) begin_dtor(begin);
                if (end) end_dtor(end);
                begin = other.begin;
                begin_copy = other.begin_copy;
                begin_dtor = other.begin_dtor;
                end = other.end;
                end_copy = other.end_copy;
                end_dtor = other.end_dtor;
                other.begin = nullptr;
                other.end = nullptr;
            }
            return *this;
        }

        constexpr ~sequence_iter_storage() {
            if (begin) begin_dtor(begin);
            if (end) end_dtor(end);
        }

        constexpr void swap(sequence_iter_storage& other) noexcept {
            std::swap(begin, other.begin);
            std::swap(begin_copy, other.begin_copy);
            std::swap(begin_dtor, other.begin_dtor);
            std::swap(end, other.end);
            std::swap(end_copy, other.end_copy);
            std::swap(end_dtor, other.end_dtor);
        }
    };

    namespace sequence_deref {
        template <meta::not_void T> requires (meta::not_rvalue<T>)
        using ptr = T(*)(void*);

        template <meta::const_ref Begin, meta::not_void T>
            requires (meta::not_rvalue<T> && meta::dereference_returns<T, Begin>)
        constexpr T _fn(void* ptr) {
            return **reinterpret_cast<meta::as_pointer<Begin>>(ptr);
        }

        template <meta::const_ref Begin, meta::not_void T>
            requires (meta::not_rvalue<T> && meta::dereference_returns<T, Begin>)
        constexpr ptr<T> fn = &_fn<Begin, T>;
    }

    namespace sequence_increment {
        using ptr = void(*)(void*);

        template <meta::lvalue Begin>
            requires (meta::not_const<Begin> && meta::has_preincrement<Begin>)
        constexpr void _fn(void* ptr) {
            ++*reinterpret_cast<meta::as_pointer<Begin>>(ptr);
        }

        template <meta::lvalue Begin>
            requires (meta::not_const<Begin> && meta::has_preincrement<Begin>)
        constexpr ptr fn = &_fn<Begin>;
    }

    namespace sequence_compare {
        using ptr = bool(*)(void*, void*);

        template <meta::const_ref Begin, meta::const_ref End>
            requires (meta::eq_returns<bool, Begin, End>)
        constexpr bool _fn(void* lhs, void* rhs) {
            return
                *reinterpret_cast<meta::as_pointer<Begin>>(lhs) ==
                *reinterpret_cast<meta::as_pointer<End>>(rhs);
        }

        template <meta::const_ref Begin, meta::const_ref End>
            requires (meta::eq_returns<bool, Begin, End>)
        constexpr ptr fn = &_fn<Begin, End>;
    }

    /// TODO: it might be best to have sequence model `std::common_range` (i.e. make
    /// the begin and end iterators always the same type).  That would require some
    /// careful design, but would make sequences more compatible with the standard
    /// library algorithms if possible.
    /// -> sequence_iter_storage would only have to store a single iterator, and the
    /// overall iterator would be initialized with the required function pointers,
    /// which would be manually generated in both cases with the right semantics.

    /* A type-erased wrapper for an iterator that dereferences to type `T` and its
    corresponding sentinel.  This is the type of iterator returned by a `sequence<T>`
    range, and always models `std::input_iterator`, as well as possibly
    `std::output_iterator<U>` if `U` is assignable to `T`. */
    template <meta::not_void T> requires (meta::not_rvalue<T>)
    struct sequence_iterator {
        using iterator_category = std::input_iterator_tag;
        using difference_type = std::ptrdiff_t;
        using value_type = meta::remove_reference<T>;
        using reference = meta::as_lvalue<T>;
        using pointer = meta::as_pointer<T>;

    private:
        sequence_iter_storage storage;
        sequence_deref::ptr<T> deref_fn = nullptr;
        sequence_increment::ptr increment_fn = nullptr;
        sequence_compare::ptr compare_fn = nullptr;

    public:
        [[nodiscard]] constexpr sequence_iterator() = default;

        template <meta::iterator Begin, meta::sentinel_for<Begin> End>
            requires (meta::dereference_returns<T, meta::as_const_ref<Begin>>)
        [[nodiscard]] constexpr sequence_iterator(Begin&& b, End&& e)
            noexcept (requires{
                {std::make_unique<meta::remove_reference<Begin>>(std::forward<Begin>(b))} noexcept;
                {std::make_unique<meta::remove_reference<End>>(std::forward<End>(e))} noexcept;
            })
        :
            storage(std::forward<Begin>(b), std::forward<End>(e)),
            deref_fn(sequence_deref::fn<meta::as_const_ref<Begin>, T>),
            increment_fn(sequence_increment::fn<meta::as_lvalue<meta::remove_const<Begin>>>),
            compare_fn(sequence_compare::fn<meta::as_const_ref<Begin>, meta::as_const_ref<End>>)
        {}

        [[nodiscard]] constexpr T operator*() const {
            return deref_fn(storage.begin);
        }

        [[nodiscard]] constexpr auto operator->() const {
            return impl::arrow_proxy(deref_fn(storage.begin));
        }

        constexpr sequence_iterator& operator++() {
            increment_fn(storage.begin);
            return *this;
        }

        [[nodiscard]] constexpr sequence_iterator operator++(int) {
            sequence_iterator tmp = *this;
            increment_fn(storage.begin);
            return tmp;
        }

        [[nodiscard]] friend constexpr bool operator==(const sequence_iterator& lhs, NoneType) {
            return lhs.compare_fn(lhs.storage.begin, lhs.storage.end);
        }

        [[nodiscard]] friend constexpr bool operator==(NoneType, const sequence_iterator& rhs) {
            return rhs.compare_fn(rhs.storage.begin, rhs.storage.end);
        }

        [[nodiscard]] friend constexpr bool operator!=(const sequence_iterator& lhs, NoneType) {
            return !lhs.compare_fn(lhs.storage.begin, lhs.storage.end);
        }

        [[nodiscard]] friend constexpr bool operator!=(NoneType, const sequence_iterator& rhs) {
            return !rhs.compare_fn(rhs.storage.begin, rhs.storage.end);
        }
    };

}


namespace iter {

    /* A type-erased range that forgets the underlying container type, and presents only as
    a sequence of some type.

    Because of their use as monadic expression templates, ranges can quickly become deeply
    nested and brittle, especially when used with conditionals which may return slightly
    different types for each branch.  A similar problem exists with C++ function objects,
    which can be mitigated by using `std::function`, which `sequence<T>` is perfectly
    analogous to, but for iterable containers.

    The sequence constructor works by taking an arbitrary container type `C` that meets
    the criteria and moving it onto the heap as a reference-counted void pointer.  It then
    generates a table of function pointers that emulate the standard range interface for
    `C`, including `size()`, `empty()`, `operator[]`, and basic input/output iterators.
    Users should note that erasing the container type in this way can substantially reduce
    iteration performance, especially for large containers and/or hot loops.  Non-erased
    ranges should therefore be preferred whenever possible, and erasure should be
    considered only as a last resort to satisfy the type checker.

    Bertrand instantiates this type internally when generating bindings for ranges that
    have no direct equivalent in the target language, and therefore cannot be translated
    normally.  In that case, as long as the range's yield type is a valid expression in
    the other language, then the rest of the range interface can be abstracted away, and
    the binding can be generated anyway, albeit at a performance cost. */
    template <meta::not_void T> requires (meta::not_rvalue<T>)
    struct sequence : range<impl::sequence<T>>, impl::sequence_tag {
        using __type = range<impl::sequence<T>>::__type;

        /* Initializing a sequence from an rvalue container will move the container onto
        the heap, where it will be reference counted until it is no longer needed.  If the
        initializer is an lvalue instead, then the sequence will simply take its address
        without relocating it or requiring an allocation.  This prevents a full copy of the
        container, but forces the user to manage the lifetime externally, ensuring that the
        underlying container always outlives the sequence. */
        template <meta::yields<T> C>
        [[nodiscard]] constexpr sequence(C&& c)
            noexcept (requires{
                {range<impl::sequence<T>>(impl::sequence<T>{std::forward<C>(c)})} noexcept;
            })
        :
            range<impl::sequence<T>>(impl::sequence<T>{std::forward<C>(c)})
        {}

        /* Swap the underlying containers for two sequences. */
        constexpr void swap(sequence& other) noexcept
            requires (requires{this->__value->swap(other.__value);})
        {
            this->__value->swap(other.__value);
        }

        /* True if the underlying container supports `size()` checks.  False otherwise. */
        [[nodiscard]] bool has_size() const noexcept
            requires (requires{this->__value->has_size();})
        {
            return this->__value->has_size();
        }

        /* Return the current size of the sequence, assuming `has_size()` evaluates to
        true.  If `has_size()` is false, and the program is compiled in debug mode, then
        this function will throw a TypeError. */
        [[nodiscard]] constexpr auto size() const {
            return this->__value->size();
        }

        /* Identical to `size()`, except that the result is a signed integer. */
        [[nodiscard]] constexpr auto ssize() const {
            return this->__value->ssize();
        }

        /* True if the underlying container supports `empty()` checks.  False otherwise. */
        [[nodiscard]] constexpr bool has_empty() const noexcept {
            return this->__value->has_empty();
        }

        /* Returns true if the underlying container is empty or false otherwise, assuming
        `has_empty()` evaluates to true.  If `has_empty()` is false, and the program is
        compiled in debug mode, then this function will throw a TypeError. */
        [[nodiscard]] constexpr bool empty() const {
            return this->__value->empty();
        }

        /* True if the underlying container supports `operator[]` accessing.  False
        otherwise. */
        [[nodiscard]] constexpr bool has_subscript() const noexcept {
            return this->__value->has_subscript();
        }

        /* Index into the sequence, applying Python-style wraparound for negative
        indices if the sequence has a known size.  Otherwise, the index must be
        non-negative, and will be converted to `size_type`.  If the index is out of
        bounds after normalizing, and the program is compiled in debug mode, then this
        function will throw a TypeError. */
        [[nodiscard]] constexpr T operator[](__type::index_type i) const {
            return (*this->__value)[i];
        }
    };

    template <meta::iterable C>
    sequence(C&& c) -> sequence<meta::remove_rvalue<meta::yield_type<C>>>;

    /* ADL `swap()` operator for type-erased sequences. */
    template <typename T>
    constexpr void swap(sequence<T>& lhs, sequence<T>& rhs)
        noexcept (requires{{lhs.swap(rhs)} noexcept;})
        requires (requires{{lhs.swap(rhs)};})
    {
        lhs.swap(rhs);
    }

}


}


#endif  // BERTRAND_ITER_SEQUENCE_H