#ifndef BERTRAND_ITER_SLICE_H
#define BERTRAND_ITER_SLICE_H

#include "bertrand/iter/range.h"


namespace bertrand {


/// TODO: maybe slice{} should also be able to accept boolean masks as step sizes,
/// which would standardize that case in the range indexing operator.


/// TODO: zip is necessary for complex slicing involving function predicates, so that's
/// a blocker for the rest of the range interface atm.


namespace impl {

    /* A normalized set of slice indices that can be used to initialize a proper slice
    range.  An instance of this class must be provided to the `impl::slice`
    constructor, and is usually produced by the `bertrand::slice{...}.normalize(ssize)`
    helper method in the case of integer indices.  Containers that allow non-integer
    indices can construct an instance of this within their own `operator[](slice)`
    method to provide custom indexing, if needed. */
    struct slice_indices {
        ssize_t start = 0;
        ssize_t stop = 0;
        ssize_t step = 1;

        [[nodiscard]] constexpr size_t size() const noexcept { return size_t(ssize()); }
        [[nodiscard]] constexpr ssize_t ssize() const noexcept {
            ssize_t bias = step + (step < 0) - (step > 0);
            ssize_t length = (stop - start + bias) / step;
            return length * (length > 0);
        }
        [[nodiscard]] constexpr bool empty() const noexcept { return ssize() == 0; }
    };



    template <meta::lvalue Self>
    struct slice_iterator;

    template <typename C>
    concept slice_container = (meta::iterable<C> && meta::has_ssize<C>) || meta::tuple_like<C>;

    template <typename F, typename C>
    concept slice_predicate =
        meta::lvalue<C> && slice_container<C> && requires(
            F f,
            impl::range_begin<C> it,
            impl::range_begin<meta::as_const_ref<C>> c_it
        ) {
            { std::forward<F>(f)(*it) } -> meta::convertible_to<bool>;
            { std::forward<F>(f)(*c_it) } -> meta::convertible_to<bool>;
        };

    template <typename F, typename C>
    concept nothrow_slice_predicate =
        meta::lvalue<C> && slice_container<C> && requires(
            F f,
            impl::range_begin<C> it,
            impl::range_begin<meta::as_const_ref<C>> c_it
        ) {
            { std::forward<F>(f)(*it) } noexcept -> meta::nothrow::convertible_to<bool>;
            { std::forward<F>(f)(*c_it) } noexcept -> meta::nothrow::convertible_to<bool>;
        };

    template <typename T, typename C>
    concept slice_param = meta::None<T> || meta::integer<T> || slice_predicate<T, C>;



    template <meta::lvalue C> requires (slice_container<C>)
    constexpr bool slice_from_tail = false;
    template <meta::lvalue C>
        requires (slice_container<C> && (
            requires(C c) {{
                impl::make_range_reversed{c}.begin()
            } -> meta::explicitly_convertible_to<impl::range_begin<C>>;} ||
            requires(C c) {{
                impl::make_range_reversed{c}.begin().base()
            } -> meta::explicitly_convertible_to<impl::range_begin<C>>;}
        ))
    constexpr bool slice_from_tail<C> = true;

    template <meta::lvalue C> requires (slice_container<C>)
    constexpr bool slice_nothrow_from_tail = false;
    template <meta::lvalue C>
        requires (slice_container<C> && (
            requires(C c) {{
                impl::make_range_reversed{c}.begin()
            } noexcept -> meta::nothrow::explicitly_convertible_to<impl::range_begin<C>>;} || (
                !requires(C c) {{
                    impl::make_range_reversed{c}.begin()
                } -> meta::explicitly_convertible_to<impl::range_begin<C>>;} &&
                requires(C c) {{
                    impl::make_range_reversed{c}.begin().base()
                } noexcept -> meta::nothrow::explicitly_convertible_to<impl::range_begin<C>>;}
            )
        ))
    constexpr bool slice_nothrow_from_tail<C> =
        (meta::nothrow::has_ssize<C> || (!meta::has_ssize<C> && meta::tuple_like<C>)) &&
        requires(C c) {
            {impl::make_range_reversed{c}.begin()} noexcept -> meta::nothrow::has_preincrement;
        };

    /* Get an iterator to the given index of an iterable or tuple-like container with a
    known size.  Note that the index is assumed to have been already normalized via
    `impl::normalize_index()` or some other means.

    This will attempt to choose the most efficient method of obtaining the
    iterator, depending on the characteristics of the container and the index.

        1.  If the iterator type is random access, then a `begin()` iterator will be
            advanced to the index in constant time.
        2.  If the iterator type is bidirectional, and reverse iterators are
            convertible to forward iterators or posses a `base()` method that returns
            a forward iterator (as is the case for `std::reverse_iterator` instances),
            and the index is closer to the end than it is to the beginning, then a
            reverse iterator will be obtained and advanced to the index before being
            converted to a forward iterator.
        3.  Otherwise, a `begin()` iterator will be obtained and advanced to the index
            using a series of increments.

    The second case ensures that as long as the preconditions are met, the worst-case
    time complexity of this operation is `O(n/2)`.  Without it, the complexity rises to
    `O(n)`.

    Implementing this operation as a free method allows it to be abstracted over any
    container (which is important for the `slice_iterator` class) and greatly
    simplifies the implementation of custom `at()` methods for user-defined types,
    which will apply the same optimizations automatically. */
    template <typename C> requires (meta::iterable<C> || meta::tuple_like<C>)
    [[nodiscard]] constexpr auto at(C& container, ssize_t index)
        noexcept (
            requires{{impl::make_range_iterator{container}.begin()} noexcept;} && (
            meta::random_access_iterator<impl::range_begin<C>> ?
                meta::nothrow::has_iadd<impl::range_begin<C>, ssize_t> :
                (!slice_from_tail<C&> || slice_nothrow_from_tail<C&>) &&
                meta::nothrow::has_preincrement<impl::range_begin<C>>
            )
        )
        requires (meta::random_access_iterator<impl::range_begin<C>> ?
            meta::has_iadd<impl::range_begin<C>, ssize_t> :
            meta::has_preincrement<impl::range_begin<C>>
        )
    {
        using wrapped = impl::range_begin<C>;

        // if the iterator supports random access, then we can just jump to the
        // start index in constant time.
        if constexpr (meta::random_access_iterator<wrapped>) {
            wrapped it = impl::make_range_iterator{container}.begin();
            it += index;
            return it;

        // otherwise, obtaining a begin iterator requires a series of increments
        // or decrements, depending on the capabilities of the range and the
        // position of the start index.
        } else {
            // if a reverse iterator is available and convertible to a forward
            // iterator, and the start index is closer to the end than it is to
            // beginning, then we can start from the end to minimize iterations.
            if constexpr (impl::slice_from_tail<C&>) {
                ssize_t size;
                if constexpr (meta::has_ssize<C&>) {
                    size = std::ranges::ssize(container);
                } else {
                    size = ssize_t(meta::tuple_size<C&>);
                }
                if (index >= ((size + 1) / 2)) {
                    auto it = impl::make_range_reversed{container}.begin();
                    for (ssize_t i = size - index; i-- > 0;) {
                        ++it;
                    }
                    if constexpr (meta::explicitly_convertible_to<decltype((it)), wrapped>) {
                        return wrapped(it);
                    } else {
                        ++it;  // it.base() trails the current element by 1
                        return wrapped(it.base());
                    }
                }
            }

            // start from the beginning and advance until the start index
            wrapped it = impl::make_range_iterator{container}.begin();
            for (ssize_t i = 0; i < index; ++i) {
                ++it;
            }
            return it;
        }
    }

    /// TODO: start and stop can be functions that take the container's yield type
    /// (both mutable and immutable) and return a boolean.  The start index will
    /// resolve to the first element that returns true, and the stop index will
    /// resolve to the first element that returns true after the start index.
    /// If the step size is given as a function, then the slice will include all the
    /// elements that meet the step condition, which allows slices to act like
    /// std::views::filter(), without need for a separate `where` operator.

    /// TODO: range predicates are complicated, since they may visit unions and
    /// decompose tuples.  That will be hard to account for in the slicing
    /// ecosystem, but I might as well start here.  That will become more robust
    /// once I implement comprehensions, which will have to do this stuff anyway.



    /// TODO: no changes are necessary to the base `slice` specialization, since it
    /// will only apply when all of the indices are integers.

    /* An adaptor for a container that causes `range<impl::slice<C, Step>>` to iterate
    over only a subset of the container according to Python-style slicing semantics.
    A range of that form will be generated by calling the public `slice{}` helper
    directly, using it to index a supported container type, or by including it in a
    range comprehension.

    The `Step` parameter represents the integer type of the step size for the slice,
    which is used to optimize the iteration logic for the slices that are guaranteed
    to have a positive step size.  This is true for any unsigned integer type as well
    as an initial slice index of `None`, which translates to `size_t` in this context.
    If the step size is signed and the underlying iterator is not random access, then
    an extra branch will be added to the core loop to check whether the iterator must
    be incremented or decremented, depending on the sign of the step size.  This is a
    niche optimization for forward-only input ranges, but is completely transparent to
    the user, and ensures zero overhead in almost all cases. */
    template <slice_container C, slice_param<C> Start, slice_param<C> Stop, slice_param<C> Step>
    struct slice {
        using type = meta::remove_rvalue<C>;  // TODO: rvalues are removed before this point
        using start_type = meta::remove_rvalue<Start>;  // TODO: same ^
        using stop_type = meta::remove_rvalue<Stop>;
        using step_type = meta::remove_rvalue<Step>;
        using size_type = size_t;
        using index_type = ssize_t;

    private:
        [[no_unique_address]] impl::ref<type> ref;
        impl::slice_indices indices;
        ssize_t _size = indices.ssize();

        constexpr ssize_t container_size() const noexcept {
            if constexpr (meta::has_ssize<type>) {
                return std::ranges::ssize(value());
            } else {
                return ssize_t(meta::tuple_size<type>);
            }
        }

    public:
        template <meta::slice S>
        [[nodiscard]] constexpr slice(meta::forward<C> c, S&& s)
            requires (requires{
                { std::forward<S>(s).start } -> std::same_as<Start>;
                { std::forward<S>(s).stop } -> std::same_as<Stop>;
                { std::forward<S>(s).step } -> std::same_as<Step>;
            })
        :
            ref(std::forward<C>(c)),
            indices(s.normalize(container_size())),
            _size(indices.ssize())
        {
            if (indices.step == 0) {
                throw ValueError("step size cannot be zero");
            }
        }

        /* Perfectly forward the underlying container according to the slice's current
        cvref qualifications. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) value(this Self&& self) noexcept {
            return (*std::forward<Self>(self).ref);
        }

        /* Indirectly access a member of the underlying container. */
        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{meta::to_arrow(value())} noexcept;})
            requires (requires{{meta::to_arrow(value())};})
        {
            return meta::to_arrow(value());
        }

        /* Indirectly access a member of the underlying container. */
        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{meta::to_arrow(value())} noexcept;})
            requires (requires{{meta::to_arrow(value())};})
        {
            return meta::to_arrow(value());
        }

        /* The normalized start index for the slice, as a signed integer.  This
        represents the first element that will be included in the slice, assuming it is
        not empty. */
        [[nodiscard]] constexpr ssize_t start() const noexcept { return indices.start; }

        /* The normalized stop index for the slice, as a signed integer.  Elements at
        or past this index will not be included in the slice, leading to a Python-style
        half-open interval. */
        [[nodiscard]] constexpr ssize_t stop() const noexcept { return indices.stop; }

        /* The normalized step size for the slice, as a signed integer.  This is always
        non-zero, and is positive for forward slices and negative for reverse slices.
        The last included index is given by `start + step * ssize`, assuming the slice
        is not empty. */
        [[nodiscard]] constexpr ssize_t step() const noexcept { return indices.step; }

        /* The total number of elements that will be included in the slice, as an
        unsigned integer. */
        [[nodiscard]] constexpr size_t size() const noexcept { return size_t(_size); }

        /* The total number of elements that will be included in the slice, as a signed
        integer. */
        [[nodiscard]] constexpr ssize_t ssize() const noexcept { return _size; }

        /* True if the slice contains no elements.  False otherwise. */
        [[nodiscard]] constexpr bool empty() const noexcept { return _size == 0; }

        /* Integer indexing operator.  Accepts a single signed integer and retrieves
        the corresponding element from the underlying container after multiplying by
        the step size and adding the start bias.  */
        template <typename Self>
        constexpr decltype(auto) operator[](this Self&& self, size_t i)
            noexcept (requires{{impl::range_subscript(
                std::forward<Self>(self).value(),
                size_t(ssize_t(i) * self.step() + self.start())
            )} noexcept;})
            requires (requires{{impl::range_subscript(
                std::forward<Self>(self).value(),
                size_t(ssize_t(i) * self.step() + self.start())
            )};})
        {
            return (impl::range_subscript(
                std::forward<Self>(self).value(),
                size_t(ssize_t(i) * self.step() + self.start())
            ));
        }

        /* Get an iterator to the start of the slice.  Incrementing the iterator will
        advance it by the given step size. */
        [[nodiscard]] constexpr auto begin()
            noexcept (requires{{impl::slice_iterator<slice&>{*this}} noexcept;})
            requires (requires{{impl::slice_iterator<slice&>{*this}};})
        {
            return impl::slice_iterator<slice&>{*this};
        }

        /* Get an iterator to the start of the slice.  Incrementing the iterator will
        advance it by the given step size. */
        [[nodiscard]] constexpr auto begin() const
            noexcept (requires{{impl::slice_iterator<const slice&>{*this}} noexcept;})
            requires (requires{{impl::slice_iterator<const slice&>{*this}};})
        {
            return impl::slice_iterator<const slice&>{*this};
        }

        /* Return a sentinel representing the end of the slice. */
        [[nodiscard]] static constexpr impl::sentinel end() noexcept { return {}; }
    };

    /* A specialization of `slice<...>` that is chosen if any of the start, stop, and
    step types are predicate functions rather than integer indices.  This prevents the
    slice from computing the indices ahead of time, and therefore yields an unsized
    range. */
    template <slice_container C, slice_param<C> Start, slice_param<C> Stop, slice_param<C> Step>
        requires (slice_predicate<Start, C> || slice_predicate<Stop, C> || slice_predicate<Step, C>)
    struct slice<C, Start, Stop, Step> {
        using type = meta::remove_rvalue<C>;
        using start_type = meta::remove_rvalue<Start>;
        using stop_type = meta::remove_rvalue<Stop>;
        using step_type = meta::remove_rvalue<Step>;
        using size_type = size_t;
        using index_type = ssize_t;

    private:
        [[no_unique_address]] impl::ref<type> ref;
        [[no_unique_address]] impl::ref<start_type> _start;
        [[no_unique_address]] impl::ref<stop_type> _stop;
        [[no_unique_address]] impl::ref<step_type> _step;

    public:
        template <meta::slice S>
        [[nodiscard]] constexpr slice(meta::forward<C> c, S&& s)
            requires (requires{
                { std::forward<S>(s).start } -> std::same_as<Start>;
                { std::forward<S>(s).stop } -> std::same_as<Stop>;
                { std::forward<S>(s).step } -> std::same_as<Step>;
            })
        :
            ref(std::forward<C>(c)),
            _start(std::forward<S>(s).start),
            _stop(std::forward<S>(s).stop),
            _step(std::forward<S>(s).step)
        {
            if constexpr (meta::integer<step_type>) {
                if (_step == 0) {
                    throw ValueError("slice step size cannot be zero");
                }
            }
        }


        /// TODO: this specialization would basically do all the same stuff, but would
        /// not have a definite size, and would evaluate the predicates lazily within
        /// the iterator.

    };

    template <slice_container C, meta::slice S>
    slice(C&&, S&& s) -> slice<
        meta::remove_rvalue<C>,
        decltype((std::forward<S>(s).start)),
        decltype((std::forward<S>(s).stop)),
        decltype((std::forward<S>(s).step))
    >;

    /// TODO: this will need specializations to account for predicate-based slices,
    /// which don't have predefined indices.
    /// -> If only the start index is a predicate, then I can continue using the faster
    /// size-based iterator, but just provide the start index in the constructor.
    /// -> If the stop index or step size are predicates, then an index-based
    /// approach will not work.  In that case, I will continue iterating until a
    /// predicate returns true or we reach the end of the range for a stop predicate.
    /// Step predicates cause us not to skip any elements, and only include those where
    /// the predicate returns true, until the stop index or predicate is reached.



    /* The overall slice iterator initializes to the start index of the slice, and
    maintains a pointer to the original slice object, whose indices it can access.
    Comparisons against other instances of the same type equate to comparisons between
    their current indices, and equality comparisons against the `None` sentinel bound
    the overall slice iteration.

    If the wrapped iterator is a random access iterator, then each increment of the
    slice iterator will equate to an `iter += step` operation on the wrapped iterator,
    which is expected to handle negative step sizes naturally.  Otherwise, a choice
    must be made between a series of `++iter` or `--iter` operations depending on the
    sign of the step size, which requires an extra branch in the core loop (unless it
    can be optimized out). */
    template <meta::lvalue Self>
    struct slice_iterator {
        using wrapped = impl::range_begin<decltype((std::declval<Self>().value()))>;
        using iterator_category = std::iterator_traits<wrapped>::iterator_category;
        using difference_type = std::iterator_traits<wrapped>::difference_type;
        using value_type = std::iterator_traits<wrapped>::value_type;
        using reference = std::iterator_traits<wrapped>::reference;
        using pointer = std::iterator_traits<wrapped>::pointer;

    private:
        using step_type = meta::unqualify<Self>::step_type;
        static constexpr bool unsigned_step =
            meta::None<step_type> || meta::unsigned_integer<step_type>;
        static constexpr bool bidirectional = meta::bidirectional_iterator<wrapped>;
        static constexpr bool random_access = meta::random_access_iterator<wrapped>;

        meta::as_pointer<Self> slice = nullptr;
        ssize_t size = 0;
        wrapped iter;

        [[nodiscard]] constexpr slice_iterator(
            meta::as_pointer<Self> slice,
            ssize_t size,
            wrapped&& iter
        )
            noexcept (meta::nothrow::movable<wrapped>)
        :
            slice(slice),
            size(size),
            iter(std::move(iter))
        {}

    public:
        [[nodiscard]] constexpr slice_iterator() = default;
        [[nodiscard]] constexpr slice_iterator(Self self)
            noexcept (
                (unsigned_step || bidirectional) &&
                requires{{impl::at(slice->value(), self.start())} noexcept;}
            )
        :
            slice(std::addressof(self)),
            size(self.ssize()),
            iter(impl::at(slice->value(), self.start()))
        {
            if constexpr (!unsigned_step && !bidirectional) {
                if (self.step() < 0) {
                    if consteval {
                        throw ValueError(
                            "cannot iterate over a forward-only range using a slice "
                            "with negative step size"
                        );
                    } else {
                        throw ValueError(
                            "cannot iterate over a forward-only range using a slice "
                            "with negative step size: " + std::to_string(self.step())
                        );
                    }
                }
            }
        }

        [[nodiscard]] constexpr decltype(auto) operator*()
            noexcept (meta::nothrow::has_dereference<wrapped&>)
            requires (meta::has_dereference<wrapped&>)
        {
            return (*iter);
        }

        [[nodiscard]] constexpr decltype(auto) operator*() const
            noexcept (meta::nothrow::has_dereference<const wrapped&>)
            requires (meta::has_dereference<const wrapped&>)
        {
            return (*iter);
        }

        [[nodiscard]] constexpr auto operator->()
            noexcept (requires{{meta::to_arrow(iter)} noexcept;})
            requires (requires{{meta::to_arrow(iter)};})
        {
            return meta::to_arrow(iter);
        }

        [[nodiscard]] constexpr auto operator->() const
            noexcept (requires{{meta::to_arrow(iter)} noexcept;})
            requires (requires{{meta::to_arrow(iter)};})
        {
            return meta::to_arrow(iter);
        }

        [[nodiscard]] constexpr decltype(auto) operator[](difference_type i)
            noexcept (requires{{iter[i * slice->step()]} noexcept;})
            requires (requires{{iter[i * slice->step()]};})
        {
            return (iter[i * slice->step()]);
        }

        [[nodiscard]] constexpr decltype(auto) operator[](difference_type i) const
            noexcept (requires{{iter[i * slice->step()]} noexcept;})
            requires (requires{{iter[i * slice->step()]};})
        {
            return (iter[i * slice->step()]);
        }

        constexpr slice_iterator& operator++()
            noexcept(random_access ?
                meta::nothrow::has_iadd<wrapped, ssize_t> :
                meta::nothrow::has_preincrement<wrapped> &&
                (unsigned_step || meta::nothrow::has_predecrement<wrapped>)
            )
            requires (random_access ?
                meta::has_iadd<wrapped, ssize_t> :
                meta::has_preincrement<wrapped>
            )
        {
            --size;
            if (size > 0) {
                ssize_t step = slice->step();
                if constexpr (random_access) {
                    iter += step;
                } else if constexpr (unsigned_step) {
                    for (ssize_t i = 0; i < step; ++i) {
                        ++iter;
                    }
                } else {
                    if (step < 0) {
                        /// NOTE: because we check on construction, we will never
                        /// enter this branch unless `--iter` is well-formed.
                        for (ssize_t i = 0; i > step; --i) {
                            --iter;
                        }
                    } else {
                        for (ssize_t i = 0; i < step; ++i) {
                            ++iter;
                        }
                    }
                }
            }
            return *this;
        }

        [[nodiscard]] constexpr slice_iterator operator++(int)
            noexcept(
                meta::nothrow::copyable<slice_iterator> &&
                meta::nothrow::has_preincrement<slice_iterator>
            )
            requires (meta::copyable<slice_iterator> && meta::has_preincrement<slice_iterator>)
        {
            slice_iterator copy = *this;
            ++*this;
            return copy;
        }

        [[nodiscard]] friend constexpr slice_iterator operator+(
            const slice_iterator& self,
            difference_type i
        )
            noexcept (requires{{self.iter + i * self.slice->step()} noexcept -> meta::is<wrapped>;})
            requires (requires{{self.iter + i * self.slice->step()} -> meta::is<wrapped>;})
        {
            return {self.slice, self.size - i, self.iter + i * self.slice->step()};
        }

        [[nodiscard]] friend constexpr slice_iterator operator+(
            difference_type i,
            const slice_iterator& self
        )
            noexcept (requires{{self.iter + i * self.slice->step()} noexcept -> meta::is<wrapped>;})
            requires (requires{{self.iter + i * self.slice->step()} -> meta::is<wrapped>;})
        {
            return {self.slice, self.size - i, self.iter + i * self.slice->step()};
        }

        constexpr slice_iterator& operator+=(difference_type i)
            noexcept (requires{{iter += i * slice->step()} noexcept;})
            requires (requires{{iter += i * slice->step()};})
        {
            size -= i;
            iter += i * slice->step();
            return *this;
        }

        constexpr slice_iterator& operator--()
            noexcept(random_access ?
                meta::nothrow::has_isub<wrapped, ssize_t> :
                meta::nothrow::has_predecrement<wrapped> &&
                (unsigned_step || meta::nothrow::has_preincrement<wrapped>)
            )
            requires (random_access ?
                meta::has_isub<wrapped, ssize_t> :
                meta::has_predecrement<wrapped>
            )
        {
            ++size;
            if (size <= slice->ssize()) {
                ssize_t step = slice->step();
                if constexpr (random_access) {
                    iter -= step;
                } else if constexpr (unsigned_step) {
                    for (ssize_t i = 0; i < step; ++i) {
                        --iter;
                    }
                } else {
                    if (step < 0) {
                        for (ssize_t i = 0; i > step; --i) {
                            ++iter;
                        }
                    } else {
                        for (ssize_t i = 0; i < step; ++i) {
                            --iter;
                        }
                    }
                }
            }
            return *this;
        }

        [[nodiscard]] constexpr slice_iterator operator--(int)
            noexcept(
                meta::nothrow::copyable<slice_iterator> &&
                meta::nothrow::has_predecrement<slice_iterator>
            )
            requires (meta::copyable<slice_iterator> && meta::has_predecrement<slice_iterator>)
        {
            slice_iterator copy = *this;
            --*this;
            return copy;
        }

        [[nodiscard]] constexpr slice_iterator operator-(difference_type i) const
            noexcept (requires{{iter - i * slice->step()} noexcept -> meta::is<wrapped>;})
            requires (requires{{iter - i * slice->step()} -> meta::is<wrapped>;})
        {
            return {slice, size + i, iter - i * slice->step()};
        }

        [[nodiscard]] constexpr difference_type operator-(const slice_iterator& other) const
            noexcept (requires{{
                (iter - other.iter) / slice->step()
            } noexcept -> meta::nothrow::convertible_to<difference_type>;})
            requires (requires{{
                (iter - other.iter) / slice->step()
            } -> meta::convertible_to<difference_type>;})
        {
            return (iter - other.iter) / slice->step();
        }

        constexpr slice_iterator& operator-=(difference_type i)
            noexcept (requires{{iter -= i * slice->step()} noexcept;})
            requires (requires{{iter -= i * slice->step()};})
        {
            size += i;
            iter -= i * slice->step();
            return *this;
        }

        [[nodiscard]] constexpr bool operator==(const slice_iterator& other) const noexcept {
            return size == other.size;
        }

        [[nodiscard]] friend constexpr bool operator==(
            const slice_iterator& self,
            impl::sentinel
        ) noexcept {
            return self.size <= 0;
        }

        [[nodiscard]] friend constexpr bool operator==(
            impl::sentinel,
            const slice_iterator& self
        ) noexcept {
            return self.size <= 0;
        }

        [[nodiscard]] constexpr bool operator!=(const slice_iterator& other) const noexcept {
            return size != other.size;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            const slice_iterator& self,
            impl::sentinel
        ) noexcept {
            return self.size > 0;
        }

        [[nodiscard]] friend constexpr bool operator!=(
            impl::sentinel,
            const slice_iterator& self
        ) noexcept {
            return self.size > 0;
        }

        [[nodiscard]] constexpr auto operator<=>(const slice_iterator& other) const noexcept {
            /// NOTE: higher size means the iterator is closer to the start, and should
            /// compare less than the other iterator
            return other.size <=> size;
        }
    };

    /* A specialization of `slice_iterator` for slices where `stop` or `step` is a
    predicate function, which prevents the indices from being computed ahead of time.

    Note that a `start` predicate is not considered, since once it has been resolved,
    the other indices can be computed normally. */
    template <meta::lvalue Self> requires (
        requires(Self self) {{ self.stop() } -> slice_predicate<decltype((self.value()))>;} ||
        requires(Self self) {{ self.step() } -> slice_predicate<decltype((self.value()))>;}
    )
    struct slice_iterator<Self> {

        /// TODO: muy complicado.  If stop is a predicate, then I would apply it when
        /// comparing against `None`, and would otherwise maintain or size, which
        /// counts to the end of the container.  The index/size would still allow
        /// ordered comparisons between iterators.

        /// TODO: if the step size is a predicate, then it removes the random access
        /// capabilities, since we're no longer jumping by a fixed size.

    };

}


namespace iter {

    /* A helper class that encapsulates the indices for a Python-style slice operator.

    This class can be used in one of 3 ways, depending on the capabilities of the container
    it is meant to slice:

        1.  Invoking the slice indices with a compatible container will promote them into a
            `range` subclass that implements generic slicing semantics via the container's
            standard iterator interface.  This allows slices to be constructed for
            arbitrary containers via `slice{start, stop, step}(container)` syntax, which
            serves as an entry point into the monadic `range` interface.
        2.  If the container implements an `operator[]` that accepts a `slice` object
            directly, then it must return a `range` subclass that implements the proper
            slicing semantics for that container.  Usually, this will simply return the
            same type as (1), invoking the `slice` with the parent container.  This allows
            for possible customization, as well as a more natural
            `container[slice{start, stop, step}]` syntax.
        3.  If the container uses the `->*` comprehension operator (as is the case for all
            Bertrand containers),  then the slice indices can be piped with it to form more
            complex expressions, such as `container ->* slice{start, stop, step}`.  This
            will use a custom `operator[]` from (2) if available or fall back to (1)
            otherwise.

    Note that in all 3 cases, the `slice` object does not do any iteration directly, and
    must be promoted to a proper range to complete the slicing operation.  See
    `impl::slice` for more details on the behavior of these ranges, and how they relate to
    the overall `range` interface. */
    template <
        meta::not_void Start = NoneType,
        meta::not_void Stop = NoneType,
        meta::not_void Step = NoneType
    >
    struct slice {
        using start_type = meta::remove_rvalue<Start>;
        using stop_type = meta::remove_rvalue<Stop>;
        using step_type = meta::remove_rvalue<Step>;
        using index_type = ssize_t;
        using indices = impl::slice_indices;

        [[no_unique_address]] start_type start;
        [[no_unique_address]] stop_type stop;
        [[no_unique_address]] step_type step;

        /* Normalize the provided indices against a container of a given size, returning a
        4-tuple with members `start`, `stop`, `step`, and `length` in that order, and
        supporting structured bindings.  If either of the original `start` or `stop`
        indices were given as negative values or `nullopt`, they will be normalized
        according to the size, and will be truncated to the nearest end if they are out
        of bounds.  `length` stores the total number of elements that will be included in
        the slice */
        [[nodiscard]] constexpr indices normalize(index_type size) const noexcept
            requires (
                ((meta::integer<start_type> && meta::has_signed<start_type>) || meta::None<start_type>) &&
                ((meta::integer<stop_type> && meta::has_signed<stop_type>) || meta::None<stop_type>) &&
                ((meta::integer<step_type> && meta::has_signed<step_type>) || meta::None<step_type>)
            )
        {
            indices result {
                .start = 0,
                .stop = size,
                .step = 1
            };

            // if no step size is given, then we can exclude negative step sizes from the
            // normalization logic
            if constexpr (meta::None<step_type>) {
                // normalize and truncate start
                if constexpr (meta::integer<start_type>) {
                    result.start = index_type(start);
                    result.start += size * (result.start < 0);
                    if (result.start < 0) {
                        result.start = 0;
                    } else {
                        result.start = size;
                    }
                }

                // normalize and truncate stop
                if constexpr (meta::integer<stop_type>) {
                    result.stop = index_type(stop);
                    result.stop += size * (result.stop < 0);
                    if (result.stop < 0) {
                        result.stop = 0;
                    } else if (result.stop >= size) {
                        result.stop = size;
                    }
                }

            // otherwise, the step size may be negative, which requires extra logic to
            // handle the wraparound and truncation correctly
            } else {
                result.step = index_type(step);
                bool sign = result.step < 0;

                // normalize and truncate start
                if constexpr (meta::None<start_type>) {
                    result.start = (size - 1) * sign;  // neg: size - 1 | pos: 0
                } else {
                    result.start = index_type(start);
                    result.start += size * (result.start < 0);
                    if (result.start < 0) {
                        result.start = -sign;  // neg: -1 | pos: 0
                    } else if (result.start >= size) {
                        result.start = size - sign;  // neg: size - 1 | pos: size
                    }
                }

                // normalize and truncate stop
                if constexpr (meta::None<stop_type>) {
                    result.stop = size * !sign - sign;  // neg: -1 | pos: size
                } else {
                    result.stop = index_type(stop);
                    result.stop += size * (result.stop < 0);
                    if (result.stop < 0) {
                        result.stop = -sign;  // neg: -1 | pos: 0
                    } else if (result.stop >= size) {
                        result.stop = size - sign;  // neg: size - 1 | pos: size
                    }
                }
            }

            return result;
        }

        /// TODO: also, there should be a special case where `start` and/or `stop` are
        /// function predicates that return true at the first element, and last element
        /// respectively.  That gets covered by the range() method and fallback case for
        /// the call operator as well.  The trick is that all of the tricky business is
        /// done in the impl::slice constructor, so that will need to be accounted for.

        /// -> Maybe this forces the slice to eagerly evaluate a begin iterator, which
        /// can simply be copied when the slice's `begin()` method is called.  This would
        /// reduce overhead slightly, at least.


        /* Promote slice consisting of only integers and/or `None` into a proper range
        subclass.  Fails to compile if the slice contains at least one non-integer
        value.

        This is identical to the fallback case for `operator()`, but is provided
        as a separate method in order to simplify custom slice operators for user-defined
        classes.  A basic implementation of such an operator could look something like
        this:
        
            ```
            struct Foo {
                // ...

                template <typename Start, typename Stop, typename Step>
                constexpr auto operator[](const slice<Start, Stop, Step>& s) const {
                    return s.range(*this);
                }

                // ...
            };
            ```

        Note that such an operator does not need to accept integer indices, and can
        implement arbitrary conversion logic by mapping the non-integer indices onto
        integer indices, and then calling this method to obtain a proper range.  Once
        defined, this class's call operator will automatically delegate to the custom
        slice operator, bypassing the usual fallback behavior. */
        template <typename C>
        [[nodiscard]] constexpr auto range(C&& container) const
            requires (
                ((meta::iterable<C> && meta::has_ssize<C>) || meta::tuple_like<C>) &&
                (meta::None<start_type> || meta::integer<start_type>) &&
                (meta::None<stop_type> || meta::integer<stop_type>) &&
                (meta::None<step_type> || meta::integer<step_type>)
            )
        {
            return iter::range(impl::slice(std::forward<C>(container), *this));
        }

        /* Forwarding call operator.  Searches for an `operator[]` overload that matches
        the given slice types and returns that result, allowing containers to customize
        the type slice behavior. */
        template <typename Self, typename C>
        [[nodiscard]] constexpr decltype(auto) operator()(this Self&& self, C&& container)
            noexcept (requires{{std::forward<C>(container)[std::forward<Self>(self)]} noexcept;})
            requires (requires{{std::forward<C>(container)[std::forward<Self>(self)]};})
        {
            return (std::forward<C>(container)[std::forward<Self>(self)]);
        }

        /* Fallback call operator, which is chosen if no `operator[]` overload can be
        found, all of the indices are integer-like or none, and the container has a
        definite `ssize()`. */
        template <typename C>
        [[nodiscard]] constexpr auto operator()(C&& container) const
            requires (
                ((meta::iterable<C> && meta::has_ssize<C>) || meta::tuple_like<C>) &&
                (meta::integer<start_type> || meta::None<start_type>) &&
                (meta::integer<stop_type> || meta::None<stop_type>) &&
                (meta::integer<step_type> || meta::None<step_type>)
            )
        {
            return range(std::forward<C>(container));
        }
    };

    template <typename Start = NoneType, typename Stop = NoneType, typename Step = NoneType>
    slice(Start&& = {}, Stop&& = {}, Step&& = {}) -> slice<
        meta::remove_rvalue<Start>,
        meta::remove_rvalue<Stop>,
        meta::remove_rvalue<Step>
    >;

}


}


#endif  // BERTRAND_ITER_SLICE_H