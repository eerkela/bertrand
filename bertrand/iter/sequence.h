#ifndef BERTRAND_ITER_SEQUENCE_H
#define BERTRAND_ITER_SEQUENCE_H

#include "bertrand/iter/range.h"


namespace bertrand {


namespace meta {

    namespace detail {

        template <typename T>
        constexpr bool sequence = false;

    }

    /* A refinement of `meta::range<T, Rs...>` that only matches type-erased sequences,
    where the underlying container type is hidden from the user.  Dereferencing the
    sequence reveals the inner type erasure mechanism. */
    template <typename T, typename... Rs>
    concept sequence =
        range<T, Rs...> && detail::sequence<unqualify<decltype(*::std::declval<T>().__value)>>;

}


namespace impl {

    template <typename T, typename Category, size_t Rank>
    concept sequence_concept =
        range_concept<T> &&
        meta::unqualified<Category> &&
        meta::inherits<Category, std::input_iterator_tag> &&
        Rank > 0;

}


namespace iter {

    template <typename T, typename Category, size_t Rank>
        requires (impl::sequence_concept<T, Category, Rank>)
    struct sequence;

}


namespace impl {

    template <typename T, typename Category, size_t Rank>
        requires (sequence_concept<T, Category, Rank>)
    struct sequence;

    template <typename T, typename Category, size_t Rank>
        requires (sequence_concept<T, Category, Rank>)
    struct sequence_iterator;

    template <typename C>
    struct _sequence_container { using type = meta::as_const<C>; };
    template <typename C> requires (!meta::iterable<meta::as_const_ref<C>> && meta::tuple_like<C>)
    struct _sequence_container<C> { using type = const impl::tuple_range<C>; };
    template <typename C> requires (!meta::iterable<meta::as_const_ref<C>> && !meta::tuple_like<C>)
    struct _sequence_container<C> { using type = const impl::scalar<C>; };
    template <typename C>
    using sequence_container = _sequence_container<C>::type;

    template <typename T, size_t Rank>
    struct _sequence_type { using type = meta::remove_rvalue<T>; };
    template <typename T, size_t Rank> requires (Rank > 0)
    struct _sequence_type<T, Rank> :
        _sequence_type<meta::yield_type<meta::as_lvalue<sequence_container<T>>>, Rank - 1>
    {};
    template <typename T, size_t Rank>
    using sequence_type = _sequence_type<T, Rank>::type;

    template <typename C> requires (meta::iterable<meta::as_const_ref<C>>)
    using sequence_category = std::conditional_t<
        std::same_as<
            meta::begin_type<meta::as_const_ref<C>>,
            meta::end_type<meta::as_const_ref<C>>
        >,
        std::conditional_t<
            meta::has_data<meta::as_const_ref<C>> && meta::inherits<
                meta::iterator_category<meta::begin_type<meta::as_const_ref<C>>>,
                std::random_access_iterator_tag
            >,
            std::contiguous_iterator_tag,
            meta::iterator_category<meta::begin_type<meta::as_const_ref<C>>>
        >,
        std::input_iterator_tag
    >;

    template <typename C, typename T, typename Category, size_t Rank>
    concept sequence_constructor =
        meta::convertible_to<sequence_type<C, Rank>, T> &&
        meta::inherits<
            meta::iterator_category<meta::begin_type<sequence_container<C>>>,
            typename std::conditional_t<
                meta::inherits<Category, std::contiguous_iterator_tag>,
                std::random_access_iterator_tag,
                Category
            >
        > &&
        meta::shape_type<sequence_container<C>>::size() == Rank;

    /* Sequences use the classic type erasure mechanism internally, consisting of a
    heap-allocated control block and an immutable void pointer to the underlying
    container.  If the container is provided as an lvalue, then the pointer will simply
    reference its current address, and no heap allocation will be performed for the
    container itself.  Otherwise, the container will be moved into a contiguous region
    immediately after the control block in order to consolidate allocations and improve
    cache locality.  In both cases, the final sequence represents a read-only view over
    the container, and does not allow for mutation of its elements.

    In addition to the container pointer, the control block also stores a family of
    immutable function pointers that implement the standard range interface for the
    type-erased container, according to the specified rank and iterator category.
    Each function pointer casts the container pointer back to its original,
    const-qualified type before invoking the corresponding operation, usually by
    delegating to a generic algorithm in the `bertrand::meta` or `bertrand::iter`
    namespaces.  Some operations may not be available for certain combinations of shape
    and category, in which case the corresponding function will be replaced with
    `None`, and will not contribute to the control block's overall size.

    The control block's lifetime (along with that of the container, if it was an
    rvalue) is gated by an atomic reference count, which allows the sequence to be
    cheaply copied and moved even if the underlying container is expensive to copy, or
    is move-only.  Note that this means that copying a sequence does not yield an
    independent copy of the underlying container like it would for ranges - both
    sequences will instead reference the same container internally.  This should be
    fine in practice since sequences are designed to be immutable, but unexpected
    behavior can occur if the original container was supplied as an lvalue, and is
    subsequently modified or destroyed while the sequence is still in use, or if user
    code casts away the constness of an element and mutates it directly.  If a deep
    copy is required, the user can implicitly convert the sequence to a container of
    the desired type using a range conversion operator, which triggers a loop over the
    sequence. */
    template <typename T, typename Category, size_t Rank>
        requires (sequence_concept<T, Category, Rank>)
    struct sequence_control {
        template <size_t N>
        struct _reduce { using type = T; };
        template <size_t N> requires (N < Rank)
        struct _reduce<N> { using type = iter::sequence<T, Category, Rank - N>; };
        template <size_t N>
        using reduce = _reduce<N>::type;

        using dtor_ptr = void(*)(sequence_control*);
        using data_ptr = std::conditional_t<
            meta::inherits<Category, std::contiguous_iterator_tag>,
            meta::as_pointer<T>(*)(sequence_control*),
            NoneType
        >;
        using shape_ptr = void(*)(sequence_control*);
        using subscript1_ptr = reduce<1>(*)(sequence_control*, ssize_t);
        using subscript2_ptr = std::conditional_t<
            (Rank >= 2),
            reduce<2>(*)(sequence_control*, ssize_t, ssize_t),
            NoneType
        >;
        using subscript3_ptr = std::conditional_t<
            (Rank >= 3),
            reduce<3>(*)(sequence_control*, ssize_t, ssize_t, ssize_t),
            NoneType
        >;
        using subscript4_ptr = std::conditional_t<
            (Rank >= 4),
            reduce<4>(*)(sequence_control*, ssize_t, ssize_t, ssize_t, ssize_t),
            NoneType
        >;
        using front_ptr = reduce<1>(*)(sequence_control*);
        using back_ptr = reduce<1>(*)(sequence_control*);
        using begin_ptr = sequence_iterator<T, Category, Rank>(*)(sequence_control*);
        using end_ptr = sequence_iterator<T, Category, Rank>(*)(sequence_control*);
        using iter_copy_ptr = void(*)(
            sequence_iterator<T, Category, Rank>&,
            const sequence_iterator<T, Category, Rank>&
        );
        using iter_assign_ptr = void(*)(
            sequence_iterator<T, Category, Rank>&,
            const sequence_iterator<T, Category, Rank>&
        );
        using iter_dtor_ptr = void(*)(sequence_iterator<T, Category, Rank>&);
        using iter_deref_ptr = reduce<1>(*)(const sequence_iterator<T, Category, Rank>&);
        using iter_subscript_ptr = std::conditional_t<
            meta::inherits<Category, std::random_access_iterator_tag>,
            reduce<1>(*)(const sequence_iterator<T, Category, Rank>&, ssize_t),
            NoneType
        >;
        using iter_increment_ptr = void(*)(sequence_iterator<T, Category, Rank>&);
        using iter_decrement_ptr = std::conditional_t<
            meta::inherits<Category, std::bidirectional_iterator_tag>,
            void(*)(sequence_iterator<T, Category, Rank>&),
            NoneType
        >;
        using iter_advance_ptr = std::conditional_t<
            meta::inherits<Category, std::random_access_iterator_tag>,
            void(*)(sequence_iterator<T, Category, Rank>&, ssize_t),
            NoneType
        >;
        using iter_retreat_ptr = iter_advance_ptr;
        using iter_distance_ptr = std::conditional_t<
            meta::inherits<Category, std::random_access_iterator_tag>,
            ssize_t(*)(
                const sequence_iterator<T, Category, Rank>&,
                const sequence_iterator<T, Category, Rank>&
            ),
            NoneType
        >;
        using iter_compare_ptr = std::strong_ordering(*)(
            const sequence_iterator<T, Category, Rank>&,
            const sequence_iterator<T, Category, Rank>&
        );

        std::atomic<size_t> refcount = 1;
        const void* const container;
        Optional<impl::shape<Rank>> shape_cache;
        std::once_flag shape_sync;
        const dtor_ptr dtor;
        const shape_ptr get_shape;
        [[no_unique_address]] const data_ptr data;
        const subscript1_ptr subscript1;
        [[no_unique_address]] const subscript2_ptr subscript2;
        [[no_unique_address]] const subscript3_ptr subscript3;
        [[no_unique_address]] const subscript4_ptr subscript4;
        const front_ptr front;
        const back_ptr back;
        const begin_ptr begin;
        const end_ptr end;
        const iter_copy_ptr iter_copy;
        const iter_assign_ptr iter_assign;
        const iter_dtor_ptr iter_dtor;
        const iter_deref_ptr iter_deref;
        [[no_unique_address]] const iter_subscript_ptr iter_subscript;
        const iter_increment_ptr iter_increment;
        [[no_unique_address]] const iter_decrement_ptr iter_decrement;
        [[no_unique_address]] const iter_advance_ptr iter_advance;
        [[no_unique_address]] const iter_retreat_ptr iter_retreat;
        [[no_unique_address]] const iter_distance_ptr iter_distance;
        const iter_compare_ptr iter_compare;

        [[nodiscard]] constexpr const impl::shape<Rank>& shape() {
            struct {
                using type = const impl::shape<Rank>&;
                sequence_control* self;
                constexpr type operator()(NoneType) {
                    self->get_shape(self);
                    return *self->shape_cache;
                }
                constexpr type operator()(type s) {
                    return s;
                }
            } visitor {this};
            return shape_cache ->* visitor;
        }

        template <typename C>
        static constexpr data_ptr get_data() noexcept {
            if constexpr (meta::inherits<Category, std::contiguous_iterator_tag>) {
                return &data_fn<C>;
            } else {
                return {};
            }
        }

        template <typename C>
        static constexpr subscript2_ptr get_subscript2() noexcept {
            if constexpr (Rank >= 2) {
                return &subscript2_fn<C>;
            } else {
                return {};
            }
        }

        template <typename C>
        static constexpr subscript3_ptr get_subscript3() noexcept {
            if constexpr (Rank >= 3) {
                return &subscript3_fn<C>;
            } else {
                return {};
            }
        }

        template <typename C>
        static constexpr subscript4_ptr get_subscript4() noexcept {
            if constexpr (Rank >= 4) {
                return &subscript4_fn<C>;
            } else {
                return {};
            }
        }

        template <typename C>
        static constexpr iter_decrement_ptr get_iter_decrement() noexcept {
            if constexpr (meta::inherits<Category, std::bidirectional_iterator_tag>) {
                return &iter_decrement_fn<C>;
            } else {
                return {};
            }
        }

        template <typename C>
        static constexpr iter_subscript_ptr get_iter_subscript() noexcept {
            if constexpr (meta::inherits<Category, std::random_access_iterator_tag>) {
                return &iter_subscript_fn<C>;
            } else {
                return {};
            }
        }

        template <typename C>
        static constexpr iter_advance_ptr get_iter_advance() noexcept {
            if constexpr (meta::inherits<Category, std::random_access_iterator_tag>) {
                return &iter_advance_fn<C>;
            } else {
                return {};
            }
        }

        template <typename C>
        static constexpr iter_retreat_ptr get_iter_retreat() noexcept {
            if constexpr (meta::inherits<Category, std::random_access_iterator_tag>) {
                return &iter_retreat_fn<C>;
            } else {
                return {};
            }
        }

        template <typename C>
        static constexpr iter_distance_ptr get_iter_distance() noexcept {
            if constexpr (meta::inherits<Category, std::random_access_iterator_tag>) {
                return &iter_distance_fn<C>;
            } else {
                return {};
            }
        }

        template <typename C>
        [[nodiscard]] static constexpr sequence_control* create(C&& c) {
            using type = meta::unqualify<C>;

            // if the container is an lvalue, then just store a pointer to it within
            // the control block
            if constexpr (meta::lvalue<C>) {
                return new sequence_control{
                    .container = std::addressof(c),
                    .dtor = &dtor_fn<C>,
                    .get_shape = &shape_fn<type>,
                    .data = get_data<type>(),
                    .subscript1 = &subscript1_fn<type>,
                    .subscript2 = get_subscript2<type>(),
                    .subscript3 = get_subscript3<type>(),
                    .subscript4 = get_subscript4<type>(),
                    .front = &front_fn<type>,
                    .back = &back_fn<type>,
                    .begin = &begin_fn<type>,
                    .end = &end_fn<type>,
                    .iter_copy = &iter_copy_fn<type>,
                    .iter_assign = &iter_assign_fn<type>,
                    .iter_dtor = &iter_dtor_fn<type>,
                    .iter_deref = &iter_deref_fn<type>,
                    .iter_subscript = get_iter_subscript<type>(),
                    .iter_increment = &iter_increment_fn<type>,
                    .iter_decrement = get_iter_decrement<type>(),
                    .iter_advance = get_iter_advance<type>(),
                    .iter_retreat = get_iter_retreat<type>(),
                    .iter_distance = get_iter_distance<type>(),
                    .iter_compare = &iter_compare_fn<type>,
                };

            // otherwise, it is possible to consolidate allocations such that the
            // container is stored immediately after the control block
            } else {
                void* control = ::operator new(
                    sizeof(sequence_control) + sizeof(meta::unqualify<C>)
                );
                if (control == nullptr) {
                    throw MemoryError();
                }
                void* value = static_cast<sequence_control*>(control) + 1;
                try {
                    new (value) meta::unqualify<C>(std::forward<C>(c));
                    try {
                        new (control) sequence_control {
                            .container = static_cast<const void*>(value),
                            .dtor = &dtor_fn<C>,
                            .get_shape = &shape_fn<type>,
                            .data = get_data<type>(),
                            .subscript1 = &subscript1_fn<type>,
                            .subscript2 = get_subscript2<type>(),
                            .subscript3 = get_subscript3<type>(),
                            .subscript4 = get_subscript4<type>(),
                            .front = &front_fn<type>,
                            .back = &back_fn<type>,
                            .begin = &begin_fn<type>,
                            .end = &end_fn<type>,
                            .iter_copy = &iter_copy_fn<type>,
                            .iter_assign = &iter_assign_fn<type>,
                            .iter_dtor = &iter_dtor_fn<type>,
                            .iter_deref = &iter_deref_fn<type>,
                            .iter_subscript = get_iter_subscript<type>(),
                            .iter_increment = &iter_increment_fn<type>,
                            .iter_decrement = get_iter_decrement<type>(),
                            .iter_advance = get_iter_advance<type>(),
                            .iter_retreat = get_iter_retreat<type>(),
                            .iter_distance = get_iter_distance<type>(),
                            .iter_compare = &iter_compare_fn<type>,
                        };
                    } catch (...) {
                        std::destroy_at(static_cast<meta::unqualify<C>*>(value));
                        throw;
                    }
                } catch (...) {
                    ::operator delete(control);
                    throw;
                }
                return static_cast<sequence_control*>(control);
            }
        }

        constexpr void incref() noexcept {
            refcount.fetch_add(1, std::memory_order_relaxed);
        }

        constexpr void decref() noexcept {
            if (refcount.fetch_sub(1, std::memory_order_release) == 1) {
                std::atomic_thread_fence(std::memory_order_acquire);
                dtor(this);
            }
        }

        template <typename C>
        static constexpr void dtor_fn(sequence_control* control) {
            // if the container is an lvalue, then only the control block needs to be
            // deallocated
            if constexpr (meta::lvalue<C>) {
                delete control;

            // otherwise, the container buffer immediately follows the control block,
            // and must also be destroyed before deallocating
            } else {
                std::destroy_at(static_cast<const meta::unqualify<C>*>(control->container));
                ::operator delete(control);
            }
        }

        template <typename C>
        static constexpr void shape_fn(sequence_control* control) {
            control->shape_cache = meta::shape(*static_cast<const C*>(control->container));
        }

        template <typename C> requires (meta::inherits<Category, std::contiguous_iterator_tag>)
        static constexpr meta::as_pointer<T> data_fn(sequence_control* control) {
            return meta::data(*static_cast<const C*>(control->container));
        }

        template <typename C>
        static constexpr reduce<1> subscript1_fn(sequence_control* control, ssize_t n);
        template <typename C> requires (Rank >= 2)
        static constexpr reduce<2> subscript2_fn(sequence_control* control, ssize_t n1, ssize_t n2);
        template <typename C> requires (Rank >= 3)
        static constexpr reduce<3> subscript3_fn(
            sequence_control* control,
            ssize_t n1,
            ssize_t n2,
            ssize_t n3
        );
        template <typename C> requires (Rank >= 4)
        static constexpr reduce<4> subscript4_fn(
            sequence_control* control,
            ssize_t n1,
            ssize_t n2,
            ssize_t n3,
            ssize_t n4
        );

        template <typename C>
        static constexpr reduce<1> front_fn(sequence_control* control) {
            return meta::front(*static_cast<const C*>(control->container));
        }

        template <typename C> requires (meta::has_back<sequence_container<C>>)
        static constexpr reduce<1> back_fn(sequence_control* control) {
            return meta::back(*static_cast<const C*>(control->container));
        }

        template <typename C> requires (!meta::has_back<sequence_container<C>>)
        static constexpr reduce<1> back_fn(sequence_control* control) {
            size_t size = control->shape().dim[0];
            auto it = meta::begin(*static_cast<const C*>(control->container));
            for (size_t i = 1; i < size; ++i) {
                ++it;
            }
            return *it;
        }

        template <typename C>
        using Begin = meta::unqualify<meta::begin_type<const C&>>;
        template <typename C>
        using End = meta::unqualify<meta::end_type<const C&>>;

        template <typename C>
        static constexpr sequence_iterator<T, Category, Rank> begin_fn(sequence_control* control) {
            // if the category is at least forward, then the begin and end iterators
            // are guaranteed to be the same type, and are stored separately to
            // maintain multi-pass requirements
            if constexpr (meta::inherits<Category, std::forward_iterator_tag>) {
                sequence_iterator<T, Category, Rank> result {
                    .control = control,
                    .iter = new Begin<C>(meta::begin(*static_cast<const C*>(control->container)))
                };
                if (result.iter == nullptr) {
                    throw MemoryError();
                }
                control->incref();
                return result;

            // otherwise, the begin and end iterators may be different types that are
            // stored together, and can be allocated at the same time
            } else {
                sequence_iterator<T, Category, Rank> result {
                    .control = control,
                    .iter = ::operator new(sizeof(Begin<C>) + sizeof(End<C>))
                };
                if (result.iter == nullptr) {
                    throw MemoryError();
                }
                try {
                    new (result.iter) Begin<C>(
                        meta::begin(*static_cast<const C*>(control->container))
                    );
                    result.sentinel = static_cast<Begin<C>*>(result.iter) + 1;
                    new (result.sentinel) End<C>(
                        meta::end(*static_cast<const C*>(control->container))
                    );
                } catch (...) {
                    ::operator delete(result.iter);
                    throw;
                }
                control->incref();
                return result;
            }
        }

        template <typename C>
        static constexpr sequence_iterator<T, Category, Rank> end_fn(sequence_control* control) {
            // if the category is at least forward, then the end iterator uses the
            // same layout as the begin iterator
            if constexpr (meta::inherits<Category, std::forward_iterator_tag>) {
                sequence_iterator<T, Category, Rank> result {
                    .control = control,
                    .iter = new End<C>(meta::end(*static_cast<const C*>(control->container)))
                };
                if (result.iter == nullptr) {
                    throw MemoryError();
                }
                control->incref();
                return result;

            // otherwise, the end iterator is trivially represented by a
            // default-constructed sequence iterator, which maintains the
            // `common_range` requirement
            } else {
                return {};
            }
        }

        /// NOTE: assumes `other` is not trivial
        template <typename C>
        static constexpr void iter_copy_fn(
            sequence_iterator<T, Category, Rank>& self,
            const sequence_iterator<T, Category, Rank>& other
        ) {
            // if the category is at least forward, then the begin and end iterators
            // are guaranteed to be the same type, and both invoke the same copy logic
            if constexpr (meta::inherits<Category, std::forward_iterator_tag>) {
                self.control = other.control;
                self.iter = new Begin<C>(*static_cast<meta::as_pointer<Begin<C>>>(other.iter));
                if (self.iter == nullptr) {
                    throw MemoryError();
                }
                self.control->incref();

            // otherwise, we need to only copy if the other iterator is not a trivial
            // sentinel, and apply the same allocation strategy as the constructor
            } else {
                if (other.iter != nullptr) {
                    self.control = other.control;
                    self.iter = ::operator new(sizeof(Begin<C>) + sizeof(End<C>));
                    self.sentinel = static_cast<Begin<C>*>(self.iter) + 1;
                    if (self.iter == nullptr) {
                        throw MemoryError();
                    }
                    try {
                        new (self.iter) Begin<C>(*static_cast<Begin<C>*>(other.iter));
                        new (self.sentinel) End<C>(*static_cast<End<C>*>(other.sentinel));
                    } catch (...) {
                        ::operator delete(self.iter);
                        throw;
                    }
                    self.control->incref();
                }
            }
        }

        /// NOTE: assumes `self` and `other` are not the same
        template <typename C>
        static constexpr void iter_assign_fn(
            sequence_iterator<T, Category, Rank>& self,
            const sequence_iterator<T, Category, Rank>& other
        ) {
            // either iterator may be in a trivial state, giving 4 cases:
            //      1.  `self` and `other` are both trivial => do nothing
            //      3.  `self` is not trivial, `other` is => deallocate
            //      2.  `self` is trivial, `other` is not => allocate and copy
            //      4.  `self` and `other` are both not trivial => direct assign
            if (other.iter == nullptr) {
                if (self.iter != nullptr) {
                    iter_dtor_fn<C>(self);
                }
            } else if (self.iter == nullptr) {
                iter_copy_fn<C>(self, other);
            } else {
                *static_cast<Begin<C>*>(self.iter) = *static_cast<Begin<C>*>(other.iter);
                if constexpr (!meta::inherits<Category, std::forward_iterator_tag>) {
                    *static_cast<End<C>*>(self.sentinel) = *static_cast<End<C>*>(other.sentinel);
                }
                self.control->decref();
                self.control = other.control;
                self.control->incref();
            }
        }

        /// NOTE: assumes `self` is not trivial
        template <typename C>
        static constexpr void iter_dtor_fn(sequence_iterator<T, Category, Rank>& self) {
            if constexpr (meta::inherits<Category, std::forward_iterator_tag>) {
                delete static_cast<Begin<C>*>(self.iter);
                self.iter = nullptr;
                self.control->decref();
                self.control = nullptr;
            } else {
                std::destroy_at(static_cast<Begin<C>*>(self.iter));
                std::destroy_at(static_cast<End<C>*>(self.sentinel));
                ::operator delete(self.iter);
                self.iter = nullptr;
                self.control->decref();
                self.control = nullptr;
            }
        }

        /// NOTE: assumes `self` is not trivial
        template <typename C>
        static constexpr reduce<1> iter_deref_fn(
            const sequence_iterator<T, Category, Rank>& self
        ) {
            return **static_cast<Begin<C>*>(self.iter);
        }

        /// NOTE: assumes `self` is not trivial
        template <typename C> requires (meta::inherits<Category, std::random_access_iterator_tag>)
        static constexpr reduce<1> iter_subscript_fn(
            const sequence_iterator<T, Category, Rank>& self,
            ssize_t n
        ) {
            return (*static_cast<Begin<C>*>(self.iter))[n];
        }

        /// NOTE: assumes `self` is not trivial
        template <typename C>
        static constexpr void iter_increment_fn(sequence_iterator<T, Category, Rank>& self) {
            ++*static_cast<Begin<C>*>(self.iter);
        }

        /// NOTE: assumes `self` is not trivial
        template <typename C> requires (meta::inherits<Category, std::bidirectional_iterator_tag>)
        static constexpr void iter_decrement_fn(sequence_iterator<T, Category, Rank>& self) {
            --*static_cast<Begin<C>*>(self.iter);
        }

        /// NOTE: assumes `self` is not trivial
        template <typename C> requires (meta::inherits<Category, std::random_access_iterator_tag>)
        static constexpr void iter_advance_fn(
            sequence_iterator<T, Category, Rank>& self,
            ssize_t n
        ) {
            *static_cast<Begin<C>*>(self.iter) += n;
        }

        /// NOTE: assumes `self` is not trivial
        template <typename C> requires (meta::inherits<Category, std::random_access_iterator_tag>)
        static constexpr void iter_retreat_fn(
            sequence_iterator<T, Category, Rank>& self,
            ssize_t n
        ) {
            *static_cast<Begin<C>*>(self.iter) -= n;
        }

        /// NOTE: assumes `self` and `other` are not trivial
        template <typename C> requires (meta::inherits<Category, std::random_access_iterator_tag>)
        static constexpr ssize_t iter_distance_fn(
            const sequence_iterator<T, Category, Rank>& lhs,
            const sequence_iterator<T, Category, Rank>& rhs
        ) {
            return *static_cast<Begin<C>*>(lhs.iter) - *static_cast<Begin<C>*>(rhs.iter);
        }

        /// NOTE: for forward iterators and higher, assumes `self` and `other` are not
        /// trivial
        template <typename C>
        static constexpr std::strong_ordering iter_compare_fn(
            const sequence_iterator<T, Category, Rank>& lhs,
            const sequence_iterator<T, Category, Rank>& rhs
        ) {
            if constexpr (meta::inherits<Category, std::forward_iterator_tag>) {
                return iter_compare_impl(
                    *static_cast<Begin<C>*>(lhs.iter),
                    *static_cast<Begin<C>*>(rhs.iter)
                );
            } else {
                if (lhs.iter == rhs.iter) {
                    return std::strong_ordering::equal;
                }
                if (lhs.iter == nullptr) {
                    return iter_compare_impl(
                        *static_cast<End<C>*>(rhs.sentinel),
                        *static_cast<Begin<C>*>(rhs.iter)
                    );
                }
                if (rhs.iter == nullptr) {
                    return iter_compare_impl(
                        *static_cast<Begin<C>*>(lhs.iter),
                        *static_cast<End<C>*>(lhs.sentinel)
                    );
                }
                return std::strong_ordering::less;  // converted into `false` in sequence_iterator
            }
        }

        template <typename LHS, typename RHS>
        static constexpr std::strong_ordering iter_compare_impl(const LHS& lhs, const RHS& rhs) {
            if constexpr (requires{{*lhs <=> *rhs};}) {
                return *lhs <=> *rhs;
            } else if constexpr (requires{{*lhs < *rhs}; {*lhs > *rhs};}) {
                if (*lhs < *rhs) {
                    return std::strong_ordering::less;
                }
                if (*lhs > *rhs) {
                    return std::strong_ordering::greater;
                }
                return std::strong_ordering::equal;
            } else {
                if (*lhs == *rhs) {
                    return std::strong_ordering::equal;
                }
                return std::strong_ordering::less;  // converted into `false` in sequence_iterator
            }
        }
    };

    /* A const iterator over a type-erased sequence, as implemented via the
    `sequence_control` block.  Iterators come in two varieties depending on the
    specified iterator category.

    If the category is `input_iterator_tag` (or an equivalent), then the iterator will
    be capable of modeling non-common ranges, where the begin and end types may differ.
    In that case, it will store void pointers to both the begin and end iterators
    internally, and the sentinel for the overall sequence will be represented by a
    default-constructed iterator where both pointers are null.  Comparison operations
    will always compare the internal iterators against each other using the
    corresponding functions from the control block, after checking for sentinels.

    If the category is at least `forward_iterator_tag`, then the underlying container
    must be a common range, and both iterators will store a single void pointer to
    their corresponding begin or end iterator, requiring separate heap allocations.
    Comparison operations can then directly compare the internal iterators without
    needing to check for sentinels, and may permit three-way comparisons if the
    category is at least `random_access_iterator_tag`.

    Note that because of the type erasure, the overall sequence will always trivially
    be both a borrowed and common range, even if the underlying container is not.  This
    also means that the iterator category for the underlying sequence may not exactly
    match the category specified by its template signature, and will always be at least
    `std::forward_iterator_tag`, owing to the common range guarantee.  Similarly, if
    the category is specified as contiguous by the template signature, but its shape
    has more than 1 dimension, then the category will be downgraded to
    `std::random_access_iterator_tag` instead. */
    template <typename T, typename Category, size_t Rank>
        requires (sequence_concept<T, Category, Rank>)
    struct sequence_iterator {
        using iterator_category = std::forward_iterator_tag;
        using difference_type = ssize_t;
        using reference = sequence_control<T, Category, Rank>::template reduce<1>;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::as_pointer<reference>;

        sequence_control<T, Category, Rank>* control = nullptr;
        void* iter = nullptr;
        void* sentinel = nullptr;

        [[nodiscard]] constexpr sequence_iterator() noexcept = default;

        [[nodiscard]] constexpr sequence_iterator(const sequence_iterator& other) :
            control(other.control)
        {
            if (control != nullptr) {
                control->iter_copy(*this, other);
            }
        }

        [[nodiscard]] constexpr sequence_iterator(sequence_iterator&& other) noexcept :
            control(other.control),
            iter(other.iter),
            sentinel(other.sentinel)
        {
            other.control = nullptr;
            other.iter = nullptr;
            other.sentinel = nullptr;
        }

        constexpr sequence_iterator& operator=(const sequence_iterator& other) {
            if (this != &other) {
                control->iter_assign(*this, other);
            }
            return *this;
        }

        constexpr sequence_iterator& operator=(sequence_iterator&& other) noexcept {
            if (this != &other) {
                if (control) {
                    control->iter_dtor(*this);
                }
                control = other.control;
                iter = other.iter;
                sentinel = other.sentinel;
                other.control = nullptr;
                other.iter = nullptr;
                other.sentinel = nullptr;
            }
            return *this;
        }

        constexpr ~sequence_iterator() {
            if (control) {
                control->iter_dtor(*this);
            }
        }

        constexpr void swap(sequence_iterator& other) noexcept {
            meta::swap(control, other.control);
            meta::swap(iter, other.iter);
            meta::swap(sentinel, other.sentinel);
        }

        [[nodiscard]] constexpr reference operator*() const {
            return control->iter_deref(*this);
        }

        [[nodiscard]] constexpr auto operator->() const {
            return impl::arrow(control->iter_deref(*this));
        }

        constexpr sequence_iterator& operator++() {
            control->iter_increment(*this);
            return *this;
        }

        [[nodiscard]] constexpr sequence_iterator operator++(int) {
            sequence_iterator tmp;
            control->iter_copy(tmp, *this);
            control->iter_increment(*this);
            return tmp;
        }

        [[nodiscard]] constexpr bool operator==(const sequence_iterator& other) const {
            return control->iter_compare(*this, other) == 0;
        }

        [[nodiscard]] constexpr bool operator!=(const sequence_iterator& other) const {
            return control->iter_compare(*this, other) != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const sequence_iterator& other) const
            requires (meta::inherits<Category, std::random_access_iterator_tag>)
        {
            return control->iter_compare(*this, other);
        }
    };
    template <typename T, typename Category, size_t Rank>
        requires (
            sequence_concept<T, Category, Rank> &&
            meta::inherits<Category, std::forward_iterator_tag>
        )
    struct sequence_iterator<T, Category, Rank> {
        using iterator_category = std::conditional_t<
            meta::inherits<Category, std::contiguous_iterator_tag>,
            std::conditional_t<
                Rank == 1,
                std::contiguous_iterator_tag,
                std::random_access_iterator_tag
            >,
            Category
        >;
        using difference_type = ssize_t;
        using reference = sequence_control<T, Category, Rank>::template reduce<1>;
        using value_type = meta::remove_reference<reference>;
        using pointer = meta::as_pointer<reference>;

        sequence_control<T, Category, Rank>* control = nullptr;
        void* iter = nullptr;

        [[nodiscard]] constexpr sequence_iterator() noexcept = default;

        [[nodiscard]] constexpr sequence_iterator(const sequence_iterator& other) :
            control(other.control)
        {
            if (control != nullptr) {
                control->iter_copy(*this, other);
            }
        }

        [[nodiscard]] constexpr sequence_iterator(sequence_iterator&& other) noexcept :
            control(other.control),
            iter(other.iter)
        {
            other.control = nullptr;
            other.iter = nullptr;
        }

        constexpr sequence_iterator& operator=(const sequence_iterator& other) {
            if (this != &other) {
                control->iter_assign(*this, other);
            }
            return *this;
        }

        constexpr sequence_iterator& operator=(sequence_iterator&& other) noexcept {
            if (this != &other) {
                if (control) {
                    control->iter_dtor(*this);
                }
                control = other.control;
                iter = other.iter;
                other.control = nullptr;
                other.iter = nullptr;
            }
            return *this;
        }

        constexpr ~sequence_iterator() {
            if (control) {
                control->iter_dtor(*this);
            }
        }

        constexpr void swap(sequence_iterator& other) noexcept {
            meta::swap(control, other.control);
            meta::swap(iter, other.iter);
        }

        [[nodiscard]] constexpr reference operator*() const {
            return control->iter_deref(*this);
        }

        [[nodiscard]] constexpr auto operator->() const {
            return impl::arrow(control->iter_deref(*this));
        }

        [[nodiscard]] constexpr decltype(auto) operator[](difference_type n) const
            requires (meta::inherits<Category, std::random_access_iterator_tag>)
        {
            return (control->iter_subscript(*this, n));
        }

        constexpr sequence_iterator& operator++() {
            control->iter_increment(*this);
            return *this;
        }

        [[nodiscard]] constexpr sequence_iterator operator++(int) {
            sequence_iterator tmp;
            control->iter_copy(tmp, *this);
            control->iter_increment(*this);
            return tmp;
        }

        constexpr sequence_iterator& operator--()
            requires (meta::inherits<Category, std::bidirectional_iterator_tag>)
        {
            control->iter_decrement(*this);
            return *this;
        }

        [[nodiscard]] constexpr sequence_iterator operator--(int)
            requires (meta::inherits<Category, std::bidirectional_iterator_tag>)
        {
            sequence_iterator tmp;
            control->iter_copy(tmp, *this);
            control->iter_decrement(*this);
            return tmp;
        }

        constexpr sequence_iterator& operator+=(difference_type n)
            requires (meta::inherits<Category, std::random_access_iterator_tag>)
        {
            control->iter_advance(*this, n);
            return *this;
        }

        [[nodiscard]] friend constexpr sequence_iterator operator+(
            const sequence_iterator& self,
            difference_type n
        )
            requires (meta::inherits<Category, std::random_access_iterator_tag>)
        {
            sequence_iterator tmp;
            self.control->iter_copy(tmp, self);
            self.control->iter_advance(tmp, n);
            return tmp;
        }

        [[nodiscard]] friend constexpr sequence_iterator operator+(
            difference_type n,
            const sequence_iterator& self
        )
            requires (meta::inherits<Category, std::random_access_iterator_tag>)
        {
            sequence_iterator tmp;
            self.control->iter_copy(tmp, self);
            self.control->iter_advance(tmp, n);
            return tmp;
        }

        [[nodiscard]] constexpr sequence_iterator& operator-=(difference_type n)
            requires (meta::inherits<Category, std::random_access_iterator_tag>)
        {
            control->iter_retreat(*this, n);
            return *this;
        }

        [[nodiscard]] constexpr sequence_iterator operator-(difference_type n) const
            requires (meta::inherits<Category, std::random_access_iterator_tag>)
        {
            sequence_iterator tmp;
            control->iter_copy(tmp, *this);
            control->iter_retreat(tmp, n);
            return tmp;
        }

        [[nodiscard]] constexpr difference_type operator-(const sequence_iterator& other) const
            requires (meta::inherits<Category, std::random_access_iterator_tag>)
        {
            return control->iter_distance(*this, other);
        }

        [[nodiscard]] constexpr bool operator==(const sequence_iterator& other) const {
            return control->iter_compare(*this, other) == 0;
        }

        [[nodiscard]] constexpr bool operator!=(const sequence_iterator& other) const {
            return control->iter_compare(*this, other) != 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const sequence_iterator& other) const
            requires (meta::inherits<Category, std::random_access_iterator_tag>)
        {
            return control->iter_compare(*this, other);
        }
    };

    /* The public-facing sequence type provides a reference-counted entry point for the
    `sequence_control` and `sequence_iterator` helpers, and delegates all behavior to
    them. */
    template <typename T, typename Category, size_t Rank>
        requires (sequence_concept<T, Category, Rank>)
    struct sequence {
        sequence_control<T, Category, Rank>* control = nullptr;

        [[nodiscard]] constexpr sequence() noexcept = default;

        template <typename C> requires (impl::sequence_constructor<C, T, Category, Rank>)
        [[nodiscard]] constexpr sequence(C&& c) :
            control(sequence_control<T, Category, Rank>::create(
                meta::to_range(std::forward<C>(c))
            ))
        {}

        [[nodiscard]] constexpr sequence(const sequence& other) noexcept : control(other.control) {
            if (control) {
                control->incref();
            }
        }

        [[nodiscard]] constexpr sequence(sequence&& other) noexcept : control(other.control) {
            other.control = nullptr;
        }

        constexpr sequence& operator=(const sequence& other) {
            if (this != &other) {
                if (control) {
                    control->decref();
                }
                control = other.control;
                if (control) {
                    control->incref();
                }
            }
            return *this;
        }

        constexpr sequence& operator=(sequence&& other) {
            if (this != &other) {
                if (control) {
                    control->decref();
                }
                control = other.control;
                other.control = nullptr;
            }
            return *this;
        }

        constexpr ~sequence() {
            if (control) {
                control->decref();
            }
        }

        constexpr void swap(sequence& other) noexcept {
            meta::swap(control, other.control);
        }

        [[nodiscard]] constexpr meta::as_pointer<T> data() const
            requires (meta::inherits<Category, std::contiguous_iterator_tag>)
        {
            return control->data(control);
        }

        [[nodiscard]] constexpr decltype(auto) shape() const {
            return control->shape();
        }

        [[nodiscard]] constexpr size_t size() const {
            return size_t(control->shape().dim[0]);
        }

        [[nodiscard]] constexpr ssize_t ssize() const {
            return ssize_t(control->shape().dim[0]);
        }

        [[nodiscard]] constexpr bool empty() const {
            return control->shape().dim[0] == 0;
        }

        [[nodiscard]] constexpr decltype(auto) operator[](ssize_t i) const {
            return (control->subscript1(control, i));
        }

        [[nodiscard]] constexpr decltype(auto) operator[](ssize_t i1, ssize_t i2) const
            requires (Rank >= 2)
        {
            return (control->subscript2(control, i1, i2));
        }

        [[nodiscard]] constexpr decltype(auto) operator[](
            ssize_t i1,
            ssize_t i2,
            ssize_t i3
        ) const requires (Rank >= 3) {
            return (control->subscript3(control, i1, i2, i3));
        }

        [[nodiscard]] constexpr decltype(auto) operator[](
            ssize_t i1,
            ssize_t i2,
            ssize_t i3,
            ssize_t i4
        ) const requires (Rank >= 4) {
            return (control->subscript4(control, i1, i2, i3, i4));
        }

        [[nodiscard]] constexpr decltype(auto) front() const {
            return (control->front(control));
        }

        [[nodiscard]] constexpr decltype(auto) back() const {
            return (control->back(control));
        }

        [[nodiscard]] constexpr auto begin() const {
            return control->begin(control);
        }

        [[nodiscard]] constexpr auto end() const {
            return control->end(control);
        }
    };

}


namespace iter {

    /* A special case of `range` that erases the underlying container type.

    Because of their use as monadic expression templates, ranges can quickly become
    deeply nested and brittle, especially when used in conditionals that may return
    slightly different types for each branch.  A similar problem exists with C++
    function objects and can be mitigated by using `std::function`, which `sequence<T>`
    is analogous to for iterable containers.

    Sequences have most of the same behaviors as normal ranges, except for:

        1.  The underlying container may be dynamically allocated (if it is not
            provided as an lvalue) along with a collection of function pointers to
            implement the type-erased interface.  The resulting control block is
            (atomically) reference counted, allowing for fast copies of the public
            `sequence` regardless of the underlying container.  However, modifying a
            copy can lead to side effects in other sequences referencing the same data.
            As a result, sequences are designed to be read-only views, and always
            const-qualify the internal container before reading from it.  Users should
            take care not to cast away these qualifiers or modify the original
            container while it is the subject of one or more sequences.
        2.  The sequence's iterators are also type-erased, and reference the same
            control block as the parent sequence.  Sequences therefore always model
            both `std::borrowed_range` and `std::common_range`, even if the underlying
            container does not.  Furthermore, the iterator category is always at least
            `std::forward_iterator`, even if the underlying container only models an
            input iterator.  Unlike the parent sequence, however, copying an iterator
            will always create a deep copy of the internal iterator state, so modifying
            one iterator will not affect any others as long as it does not modify the
            underlying container.
        3.  The sequence's `shape()` is cached within the control block, and reused to
            implement the `size()`, `ssize()`, and `empty()` methods.  These methods
            are thus always supported regardless of the underlying container, but may
            require a linear traversal to infer the proper shape, which will then be
            cached for future use.
        4.  The sequence type is never tuple-like, and only permits integer indexing
            via `operator[]`.  Multi-dimensional indexing is still allowed as long as
            the container's rank supports it, possibly requiring additional heap
            allocations for nested sequences between the outermost sequence and the
            final yield type.

    Note that erasing the container type in this way can substantially reduce iteration
    performance, especially for large containers and/or hot loops.  Non-erased ranges
    should therefore be preferred whenever possible, and erasure should only be
    considered as a last resort to satisfy the type checker, or when the convenience
    outweighs the performance cost.  Bertrand uses sequences internally to generate
    bindings for ranges that have no direct equivalent in a target language - such as
    anonymous generator expressions or naked iterators, which would otherwise require
    unique bindings for each occurrence (and therefore an explosion of types).  Such
    containers can be replaced by sequences as long as their yield type has been
    exported to the target language in order to avoid the explosion. */
    template <typename T, typename Category = std::input_iterator_tag, size_t Rank = 1>
        requires (impl::sequence_concept<T, Category, Rank>)
    struct sequence : range<impl::sequence<
        meta::yield_type<meta::as_const_ref<T>>,
        Category,
        Rank
    >> {
        [[nodiscard]] constexpr sequence() = default;
        template <typename C> requires (impl::sequence_constructor<C, T, Category, Rank>)
        [[nodiscard]] constexpr sequence(C&& c) :
            range<impl::sequence<meta::yield_type<meta::as_const_ref<T>>, Category, Rank>>(
                meta::to_range(std::forward<C>(c))
            )
        {}
        using range<impl::sequence<
            meta::yield_type<meta::as_const_ref<T>>,
            Category,
            Rank
        >>::operator=;
    };

    template <typename C>
    sequence(C&& c) -> sequence<
        impl::sequence_type<C, meta::shape_type<impl::sequence_container<C>>::size()>,
        impl::sequence_category<C>,
        meta::shape_type<impl::sequence_container<C>>::size()
    >;

}


namespace meta {

    namespace detail {

        template <typename T, typename Category, size_t Rank>
        constexpr bool sequence<impl::sequence<T, Category, Rank>> = true;

    }

}


namespace impl {

    template <typename T, typename Category, size_t Rank>
        requires (sequence_concept<T, Category, Rank>)
    template <typename C>
    constexpr auto sequence_control<T, Category, Rank>::subscript1_fn(
        sequence_control* control,
        ssize_t n
    ) -> sequence_control<T, Category, Rank>::reduce<1> {
        return reduce<1>(iter::at{n}(*static_cast<const C*>(control->container)));
    }

    template <typename T, typename Category, size_t Rank>
        requires (sequence_concept<T, Category, Rank>)
    template <typename C> requires (Rank >= 2)
    constexpr auto sequence_control<T, Category, Rank>::subscript2_fn(
        sequence_control* control,
        ssize_t n1,
        ssize_t n2
    ) -> sequence_control<T, Category, Rank>::reduce<2> {
        return reduce<2>(iter::at{n1, n2}(*static_cast<const C*>(control->container)));
    }

    template <typename T, typename Category, size_t Rank>
        requires (sequence_concept<T, Category, Rank>)
    template <typename C> requires (Rank >= 3)
    constexpr auto sequence_control<T, Category, Rank>::subscript3_fn(
        sequence_control* control,
        ssize_t n1,
        ssize_t n2,
        ssize_t n3
    ) -> sequence_control<T, Category, Rank>::reduce<3> {
        return reduce<3>(iter::at{n1, n2, n3}(
            *static_cast<const C*>(control->container)
        ));
    }

    template <typename T, typename Category, size_t Rank>
        requires (sequence_concept<T, Category, Rank>)
    template <typename C> requires (Rank >= 4)
    constexpr auto sequence_control<T, Category, Rank>::subscript4_fn(
        sequence_control* control,
        ssize_t n1,
        ssize_t n2,
        ssize_t n3,
        ssize_t n4
    ) -> sequence_control<T, Category, Rank>::reduce<4> {
        return reduce<4>(iter::at{n1, n2, n3, n4}(
            *static_cast<const C*>(control->container)
        ));
    }

}


}  // namespace bertrand


_LIBCPP_BEGIN_NAMESPACE_STD

    namespace ranges {

        template <typename T, typename Category, size_t Rank>
        constexpr bool enable_borrowed_range<bertrand::impl::sequence<T, Category, Rank>> = true;

    }

_LIBCPP_END_NAMESPACE_STD


#endif  // BERTRAND_ITER_SEQUENCE_H