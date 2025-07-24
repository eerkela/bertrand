#ifndef BERTRAND_TUPLE_H
#define BERTRAND_TUPLE_H


#include "bertrand/common.h"
#include "bertrand/except.h"
#include "bertrand/union.h"
#include "bertrand/iter.h"
#include "bertrand/static_str.h"


namespace bertrand {


/// TODO: this is all left over from the range implementation.  It's basically correct,
/// but tuple_storage now doesn't need to be an overload set, and it's all defined
/// after strings.  That enables named tuple support, possibly even with reflection,
/// whereby something like the following would work:

///     Tuple t("x"_ = 2, "y"_ = 3.14);
///     auto x = t.x;
///     auto y = t.y;
///     auto x2 = t.get<"x">();
///     auto y2 = t["y"];


namespace impl {

    /* Unless they happen to all consist of a single type, tuples use recursive
    inheritance */
    template <typename...>
    struct _tuple_storage : tuple_storage_tag {
        using types = meta::pack<>;
        constexpr void swap(_tuple_storage&) noexcept {}
        template <size_t I, typename Self> requires (false)  // never actually called
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept;
    };

    template <typename T, typename... Ts>
    struct _tuple_storage<T, Ts...> : _tuple_storage<Ts...> {
    private:
        using type = meta::remove_rvalue<T>;

    public:
        [[no_unique_address]] type data;

        [[nodiscard]] constexpr _tuple_storage() = default;
        [[nodiscard]] constexpr _tuple_storage(T val, Ts... rest)
            noexcept (
                meta::nothrow::convertible_to<T, type> &&
                meta::nothrow::constructible_from<_tuple_storage<Ts...>, Ts...>
            )
            requires (
                meta::convertible_to<T, type> &&
                meta::constructible_from<_tuple_storage<Ts...>, Ts...>
            )
        :
            _tuple_storage<Ts...>(std::forward<Ts>(rest)...),
            data(std::forward<T>(val))
        {}

        constexpr void swap(_tuple_storage& other)
            noexcept (
                meta::nothrow::swappable<meta::remove_rvalue<T>> &&
                meta::nothrow::swappable<_tuple_storage<Ts...>>
            )
            requires (
                meta::swappable<meta::remove_rvalue<T>> &&
                meta::swappable<_tuple_storage<Ts...>>
            )
        {
            _tuple_storage<Ts...>::swap(other);
            std::ranges::swap(data, other.data);
        }

        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{
                {std::forward<Self>(self).data(std::forward<A>(args)...)} noexcept;
            })
            requires (
                requires{{std::forward<Self>(self).data(std::forward<A>(args)...)};} &&
                !requires{{std::forward<meta::qualify<_tuple_storage<Ts...>, Self>>(self)(
                    std::forward<A>(args)...
                )};}
            )
        {
            return (std::forward<Self>(self).data(std::forward<A>(args)...));
        }

        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{{std::forward<meta::qualify<_tuple_storage<Ts...>, Self>>(self)(
                std::forward<A>(args)...
            )} noexcept;})
            requires (
                !requires{{std::forward<Self>(self).data(std::forward<A>(args)...)};} &&
                requires{{std::forward<meta::qualify<_tuple_storage<Ts...>, Self>>(self)(
                    std::forward<A>(args)...
                )};}
            )
        {
            using base = meta::qualify<_tuple_storage<Ts...>, Self>;
            return (std::forward<base>(self)(std::forward<A>(args)...));
        }

        template <size_t I, typename Self> requires (I < sizeof...(Ts) + 1)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            if constexpr (I == 0) {
                return (std::forward<Self>(self).data);
            } else {
                using base = meta::qualify<_tuple_storage<Ts...>, Self>;
                return (std::forward<base>(self).template get<I - 1>());
            }
        }
    };

    template <meta::lvalue T, typename... Ts>
    struct _tuple_storage<T, Ts...> : _tuple_storage<Ts...> {
        [[no_unique_address]] struct { T ref; } data;

        /// NOTE: no default constructor for lvalue references
        [[nodiscard]] constexpr _tuple_storage(T ref, Ts... rest)
            noexcept (meta::nothrow::constructible_from<_tuple_storage<Ts...>, Ts...>)
            requires (meta::constructible_from<_tuple_storage<Ts...>, Ts...>)
        :
            _tuple_storage<Ts...>(std::forward<Ts>(rest)...),
            data{ref}
        {}

        constexpr _tuple_storage(const _tuple_storage&) = default;
        constexpr _tuple_storage(_tuple_storage&&) = default;
        constexpr _tuple_storage& operator=(const _tuple_storage& other) {
            _tuple_storage<Ts...>::operator=(other);
            std::construct_at(&data, other.data.ref);
            return *this;
        };
        constexpr _tuple_storage& operator=(_tuple_storage&& other) {
            _tuple_storage<Ts...>::operator=(std::move(other));
            std::construct_at(&data, other.data.ref);
            return *this;
        };

        constexpr void swap(_tuple_storage& other)
            noexcept (meta::nothrow::swappable<_tuple_storage<Ts...>>)
            requires (meta::swappable<_tuple_storage<Ts...>>)
        {
            _tuple_storage<Ts...>::swap(other);
            auto tmp = data;
            std::construct_at(&data, other.data.ref);
            std::construct_at(&other.data, tmp.ref);
        }

        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{
                {std::forward<Self>(self).data.ref(std::forward<A>(args)...)} noexcept;
            })
            requires (
                requires{{std::forward<Self>(self).data.ref(std::forward<A>(args)...)};} &&
                !requires{{std::forward<meta::qualify<_tuple_storage<Ts...>, Self>>(self)(
                    std::forward<A>(args)...
                )};}
            )
        {
            return (std::forward<Self>(self).data.ref(std::forward<A>(args)...));
        }

        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{{std::forward<meta::qualify<_tuple_storage<Ts...>, Self>>(self)(
                std::forward<A>(args)...
            )} noexcept;})
            requires (
                !requires{{std::forward<Self>(self).data.ref(std::forward<A>(args)...)};} &&
                requires{{std::forward<meta::qualify<_tuple_storage<Ts...>, Self>>(self)(
                    std::forward<A>(args)...
                )};}
            )
        {
            using base = meta::qualify<_tuple_storage<Ts...>, Self>;
            return (std::forward<base>(self)(std::forward<A>(args)...));
        }

        template <size_t I, typename Self> requires (I < sizeof...(Ts) + 1)
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            if constexpr (I == 0) {
                return (std::forward<Self>(self).data.ref);
            } else {
                using base = meta::qualify<_tuple_storage<Ts...>, Self>;
                return (std::forward<base>(self).template get<I - 1>());
            }
        }
    };

    /* A basic implementation of a tuple using recursive inheritance, meant to be used
    in conjunction with `union_storage` as the basis for further algebraic types.
    Tuples of this form can be destructured just like `std::tuple`, invoked as if they
    were overload sets, and iterated over/indexed like an array, possibly yielding
    `Union`s if the tuple types are heterogeneous. */
    template <meta::not_void... Ts>
    struct tuple_storage : _tuple_storage<Ts...> {
        using types = meta::pack<Ts...>;
        using size_type = size_t;
        using index_type = ssize_t;
        using iterator = tuple_iterator<tuple_storage&>;
        using const_iterator = tuple_iterator<const tuple_storage&>;
        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        using _tuple_storage<Ts...>::_tuple_storage;

        /* Return the total number of elements within the tuple, as an unsigned
        integer. */
        [[nodiscard]] static constexpr size_type size() noexcept {
            return sizeof...(Ts);
        }

        /* Return the total number of elements within the tuple, as a signed
        integer. */
        [[nodiscard]] static constexpr index_type ssize() noexcept {
            return index_type(size());
        }

        /* Return true if the tuple holds no elements. */
        [[nodiscard]] static constexpr bool empty() noexcept {
            return size() == 0;
        }

        /* Perfectly forward the value at a specific index, where that index is known
        at compile time. */
        template <size_type I, typename Self> requires (I < size())
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            using base = meta::qualify<_tuple_storage<Ts...>, Self>;
            return (std::forward<base>(self).template get<I>());
        }

        /* Swap the contents of two tuples with the same type specification. */
        constexpr void swap(tuple_storage& other)
            noexcept (meta::nothrow::swappable<_tuple_storage<Ts...>>)
            requires (meta::swappable<_tuple_storage<Ts...>>)
        {
            if (this != &other) {
                _tuple_storage<Ts...>::swap(other);
            }
        }

        /* Index into the tuple, perfectly forwarding the result according to the
        tuple's current cvref qualifications. */
        template <typename Self>
        [[nodiscard]] constexpr decltype(auto) operator[](this Self&& self)
            noexcept (tuple_array<meta::forward<Self>>::nothrow)
            requires (
                tuple_array<meta::forward<Self>>::kind != tuple_array_kind::NO_COMMON_TYPE &&
                tuple_array<meta::forward<Self>>::kind != tuple_array_kind::EMPTY
            )
        {
            return (tuple_array<meta::forward<Self>>::template tbl<>[
                self._value.index()
            ](std::forward<Self>(self)));
        }

        /* Get an iterator to a specific index of the tuple. */
        [[nodiscard]] constexpr iterator at(size_type index)
            noexcept (requires{{iterator{*this, index}} noexcept;})
            requires (requires{{iterator{*this, index}};})
        {
            return {*this, index_type(index)};
        }

        /* Get an iterator to a specific index of the tuple. */
        [[nodiscard]] constexpr const_iterator at(size_type index) const
            noexcept (requires{{const_iterator{*this, index}} noexcept;})
            requires (requires{{const_iterator{*this, index}};})
        {
            return {*this, index_type(index)};
        }

        /* Get an iterator to the first element in the tuple, or one past that if the
        tuple is empty. */
        [[nodiscard]] constexpr iterator begin()
            noexcept (requires{{iterator{*this, 0}} noexcept;})
            requires (requires{{iterator{*this, 0}};})
        {
            return {*this, 0};
        }

        /* Get an iterator to the first element in the tuple, or one past that if the
        tuple is empty. */
        [[nodiscard]] constexpr const_iterator begin() const
            noexcept (requires{{const_iterator{*this, 0}} noexcept;})
            requires (requires{{const_iterator{*this, 0}};})
        {
            return {*this, 0};
        }

        /* Get an iterator to the first element in the tuple, or one past that if the
        tuple is empty. */
        [[nodiscard]] constexpr const_iterator cbegin() const
            noexcept (requires{{const_iterator{*this, 0}} noexcept;})
            requires (requires{{const_iterator{*this, 0}};})
        {
            return {*this, 0};
        }

        /* Get an iterator to one past the last element in the tuple. */
        [[nodiscard]] constexpr iterator end()
            noexcept (requires{{iterator{ssize()}} noexcept;})
            requires (requires{{iterator{ssize()}};})
        {
            return {ssize()};
        }

        /* Get an iterator to one past the last element in the tuple. */
        [[nodiscard]] constexpr const_iterator end() const
            noexcept (requires{{const_iterator{ssize()}} noexcept;})
            requires (requires{{const_iterator{ssize()}};})
        {
            return {ssize()};
        }

        /* Get an iterator to one past the last element in the tuple. */
        [[nodiscard]] constexpr const_iterator cend() const
            noexcept (requires{{const_iterator{ssize()}} noexcept;})
            requires (requires{{const_iterator{ssize()}};})
        {
            return {ssize()};
        }

        /* Get a reverse iterator to the last element in the tuple. */
        [[nodiscard]] constexpr reverse_iterator rbegin()
            noexcept (requires{{std::make_reverse_iterator(end())} noexcept;})
            requires (requires{{std::make_reverse_iterator(end())};})
        {
            return std::make_reverse_iterator(end());
        }

        /* Get a reverse iterator to the last element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator rbegin() const
            noexcept (requires{{std::make_reverse_iterator(end())} noexcept;})
            requires (requires{{std::make_reverse_iterator(end())};})
        {
            return std::make_reverse_iterator(end());
        }

        /* Get a reverse iterator to the last element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator crbegin() const
            noexcept (requires{{std::make_reverse_iterator(cend())} noexcept;})
            requires (requires{{std::make_reverse_iterator(cend())};})
        {
            return std::make_reverse_iterator(cend());
        }

        /* Get a reverse iterator to one before the first element in the tuple. */
        [[nodiscard]] constexpr reverse_iterator rend()
            noexcept (requires{{std::make_reverse_iterator(begin())} noexcept;})
            requires (requires{{std::make_reverse_iterator(begin())};})
        {
            return std::make_reverse_iterator(begin());
        }

        /* Get a reverse iterator to one before the first element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator rend() const
            noexcept (requires{{std::make_reverse_iterator(begin())} noexcept;})
            requires (requires{{std::make_reverse_iterator(begin())};})
        {
            return std::make_reverse_iterator(begin());
        }

        /* Get a reverse iterator to one before the first element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator crend() const
            noexcept (requires{{std::make_reverse_iterator(cbegin())} noexcept;})
            requires (requires{{std::make_reverse_iterator(cbegin())};})
        {
            return std::make_reverse_iterator(cbegin());
        }
    };

    /* A special case of `tuple_storage<Ts...>` where all `Ts...` are identical,
    allowing the storage layout to optimize to a flat array instead of requiring
    recursive base classes, speeding up both compilation and indexing/iteration. */
    template <meta::not_void T, meta::not_void... Ts> requires (std::same_as<T, Ts> && ...)
    struct tuple_storage<T, Ts...> : tuple_storage_tag {
        using types = meta::pack<T, Ts...>;
        using size_type = size_t;
        using index_type = ssize_t;

        /* Return the total number of elements within the tuple, as an unsigned integer. */
        [[nodiscard]] static constexpr size_type size() noexcept {
            return sizeof...(Ts) + 1;
        }

        /* Return the total number of elements within the tuple, as a signed integer. */
        [[nodiscard]] static constexpr index_type ssize() noexcept {
            return index_type(size());
        }

        /* Return true if the tuple holds no elements. */
        [[nodiscard]] static constexpr bool empty() noexcept {
            return size() == 0;
        }

    private:
        struct store { meta::remove_rvalue<T> value; };
        using array = std::array<store, size()>;

    public:
        struct iterator {
            using iterator_category = std::contiguous_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = meta::remove_reference<T>;
            using reference = meta::as_lvalue<value_type>;
            using pointer = meta::as_pointer<value_type>;

            store* ptr;

            [[nodiscard]] constexpr reference operator*() const noexcept {
                return ptr->value;
            }

            /// TODO: use meta::to_arrow instead
            [[nodiscard]] constexpr pointer operator->() const
                noexcept (meta::nothrow::address_returns<pointer, reference>)
                requires (meta::address_returns<pointer, reference>)
            {
                return std::addressof(ptr->value);
            }

            [[nodiscard]] constexpr reference operator[](difference_type n) const noexcept {
                return ptr[n].value;
            }

            constexpr iterator& operator++() noexcept {
                ++ptr;
                return *this;
            }

            [[nodiscard]] constexpr iterator operator++(int) noexcept {
                iterator tmp = *this;
                ++ptr;
                return tmp;
            }

            [[nodiscard]] friend constexpr iterator operator+(
                const iterator& self,
                difference_type n
            ) noexcept {
                return {self.ptr + n};
            }

            [[nodiscard]] friend constexpr iterator operator+(
                difference_type n,
                const iterator& self
            ) noexcept {
                return {self.ptr + n};
            }

            constexpr iterator& operator+=(difference_type n) noexcept {
                ptr += n;
                return *this;
            }

            constexpr iterator& operator--() noexcept {
                --ptr;
                return *this;
            }

            [[nodiscard]] constexpr iterator operator--(int) noexcept {
                iterator tmp = *this;
                --ptr;
                return tmp;
            }

            [[nodiscard]] constexpr iterator operator-(difference_type n) const noexcept {
                return {ptr - n};
            }

            [[nodiscard]] constexpr difference_type operator-(const iterator& other) const noexcept {
                return ptr - other.ptr;
            }

            [[nodiscard]] constexpr bool operator==(const iterator& other) const noexcept {
                return ptr == other.ptr;
            }

            [[nodiscard]] constexpr auto operator<=>(const iterator& other) const noexcept {
                return ptr <=> other.ptr;
            }
        };

        struct const_iterator {
            using iterator_category = std::contiguous_iterator_tag;
            using difference_type = std::ptrdiff_t;
            using value_type = meta::remove_reference<meta::as_const<T>>;
            using reference = meta::as_lvalue<value_type>;
            using pointer = meta::as_pointer<value_type>;

            const store* ptr;

            [[nodiscard]] constexpr reference operator*() const noexcept {
                return ptr->value;
            }

            /// TODO: use meta::to_arrow instead
            [[nodiscard]] constexpr pointer operator->() const
                noexcept (meta::nothrow::address_returns<pointer, reference>)
                requires (meta::address_returns<pointer, reference>)
            {
                return std::addressof(ptr->value);
            }

            [[nodiscard]] constexpr reference operator[](difference_type n) const noexcept {
                return ptr[n].value;
            }

            constexpr const_iterator& operator++() noexcept {
                ++ptr;
                return *this;
            }

            [[nodiscard]] constexpr const_iterator operator++(int) noexcept {
                const_iterator tmp = *this;
                ++ptr;
                return tmp;
            }

            [[nodiscard]] friend constexpr const_iterator operator+(
                const const_iterator& self,
                difference_type n
            ) noexcept {
                return {self.ptr + n};
            }

            [[nodiscard]] friend constexpr const_iterator operator+(
                difference_type n,
                const const_iterator& self
            ) noexcept {
                return {self.ptr + n};
            }

            constexpr const_iterator& operator+=(difference_type n) noexcept {
                ptr += n;
                return *this;
            }

            constexpr const_iterator& operator--() noexcept {
                --ptr;
                return *this;
            }

            [[nodiscard]] constexpr const_iterator operator--(int) noexcept {
                const_iterator tmp = *this;
                --ptr;
                return tmp;
            }

            [[nodiscard]] constexpr const_iterator operator-(difference_type n) const noexcept {
                return {ptr - n};
            }

            [[nodiscard]] constexpr difference_type operator-(
                const const_iterator& other
            ) const noexcept {
                return ptr - other.ptr;
            }

            [[nodiscard]] constexpr bool operator==(const const_iterator& other) const noexcept {
                return ptr == other.ptr;
            }

            [[nodiscard]] constexpr auto operator<=>(const const_iterator& other) const noexcept {
                return ptr <=> other.ptr;
            }
        };

        using reverse_iterator = std::reverse_iterator<iterator>;
        using const_reverse_iterator = std::reverse_iterator<const_iterator>;

        array data;

        [[nodiscard]] constexpr tuple_storage()
            noexcept (meta::nothrow::default_constructible<meta::remove_rvalue<T>>)
            requires (!meta::lvalue<T> && meta::default_constructible<meta::remove_rvalue<T>>)
        :
            data{}
        {}

        [[nodiscard]] constexpr tuple_storage(T val, Ts... rest)
            noexcept (requires{
                {array{store{std::forward<T>(val)}, store{std::forward<Ts>(rest)}...}} noexcept;
            })
            requires (requires{
                {array{store{std::forward<T>(val)}, store{std::forward<Ts>(rest)}...}};
            })
        :
            data{store{std::forward<T>(val)}, store{std::forward<Ts>(rest)}...}
        {}

        [[nodiscard]] constexpr tuple_storage(const tuple_storage&) = default;
        [[nodiscard]] constexpr tuple_storage(tuple_storage&&) = default;

        constexpr tuple_storage& operator=(const tuple_storage& other)
            noexcept (meta::lvalue<T> || meta::nothrow::copy_assignable<meta::remove_rvalue<T>>)
            requires (meta::lvalue<T> || meta::copy_assignable<meta::remove_rvalue<T>>)
        {
            if constexpr (meta::lvalue<T>) {
                if (this != &other) {
                    for (size_type i = 0; i < size(); ++i) {
                        std::construct_at(&data[i].value, other.data[i].value);
                    }
                }
            } else {
                data = other.data;
            }
            return *this;
        }

        constexpr tuple_storage& operator=(tuple_storage&& other)
            noexcept (meta::lvalue<T> || meta::nothrow::move_assignable<meta::remove_rvalue<T>>)
            requires (meta::lvalue<T> || meta::move_assignable<meta::remove_rvalue<T>>)
        {
            if constexpr (meta::lvalue<T>) {
                if (this != &other) {
                    for (size_type i = 0; i < size(); ++i) {
                        std::construct_at(&data[i].value, other.data[i].value);
                    }
                }
            } else {
                data = std::move(other).data;
            }
            return *this;
        }

        /* Swap the contents of two tuples with the same type specification. */
        constexpr void swap(tuple_storage& other)
            noexcept (meta::lvalue<T> || meta::nothrow::swappable<meta::remove_rvalue<T>>)
            requires (meta::lvalue<T> || meta::swappable<meta::remove_rvalue<T>>)
        {
            if (this != &other) {
                for (size_type i = 0; i < size(); ++i) {
                    if constexpr (meta::lvalue<T>) {
                        store tmp = data[i];
                        std::construct_at(&data[i].value, other.data[i].value);
                        std::construct_at(&other.data[i].value, tmp.value);
                    } else {
                        std::ranges::swap(data[i].value, other.data[i].value);
                    }
                }
            }
        }

        /* Invoke the tuple as an overload set, assuming precisely one element is
        invocable with the given arguments. */
        template <typename Self, typename... A>
        constexpr decltype(auto) operator()(this Self&& self, A&&... args)
            noexcept (requires{
                {std::forward<Self>(self).data[0].value(std::forward<A>(args)...)} noexcept;
            })
            requires (size() == 1 && requires{
                {std::forward<Self>(self).data[0].value(std::forward<A>(args)...)};
            })
        {
            return (std::forward<Self>(self).data[0].value(std::forward<A>(args)...));
        }

        /* Perfectly forward the value at a specific index, where that index is known
        at compile time. */
        template <size_t I, typename Self> requires (I < size())
        [[nodiscard]] constexpr decltype(auto) get(this Self&& self) noexcept {
            return (std::forward<Self>(self).data[I].value);
        }

        /* Index into the tuple, perfectly forwarding the result according to the
        tuple's current cvref qualifications. */
        template <typename Self>
        constexpr decltype(auto) operator[](this Self&& self, size_type index) noexcept {
            return (std::forward<Self>(self).data[index].value);
        }

        /* Get an iterator to a specific index of the tuple. */
        [[nodiscard]] constexpr iterator at(size_type index) noexcept {
            return {data.data() + index};
        }

        /* Get an iterator to a specific index of the tuple. */
        [[nodiscard]] constexpr const_iterator at(size_type index) const noexcept {
            return {data.data() + index};
        }

        /* Get an iterator to the first element in the tuple, or one past that if the
        tuple is empty. */
        [[nodiscard]] constexpr iterator begin() noexcept {
            return {data.data()};
        }

        /* Get an iterator to the first element in the tuple, or one past that if the
        tuple is empty. */
        [[nodiscard]] constexpr const_iterator begin() const noexcept {
            return {data.data()};
        }

        /* Get an iterator to the first element in the tuple, or one past that if the
        tuple is empty. */
        [[nodiscard]] constexpr const_iterator cbegin() const noexcept {
            return {data.data()};
        }

        /* Get an iterator to one past the last element in the tuple. */
        [[nodiscard]] constexpr iterator end() noexcept {
            return {data.data() + size()};
        }

        /* Get an iterator to one past the last element in the tuple. */
        [[nodiscard]] constexpr const_iterator end() const noexcept {
            return {data.data() + size()};
        }

        /* Get an iterator to one past the last element in the tuple. */
        [[nodiscard]] constexpr const_iterator cend() const noexcept {
            return {data.data() + size()};
        }

        /* Get a reverse iterator to the last element in the tuple. */
        [[nodiscard]] constexpr reverse_iterator rbegin() noexcept {
            return std::make_reverse_iterator(end());
        }

        /* Get a reverse iterator to the last element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator rbegin() const noexcept {
            return std::make_reverse_iterator(end());
        }

        /* Get a reverse iterator to the last element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator crbegin() const noexcept {
            return std::make_reverse_iterator(cend());
        }

        /* Get a reverse iterator to one before the first element in the tuple. */
        [[nodiscard]] constexpr reverse_iterator rend() noexcept {
            return std::make_reverse_iterator(begin());
        }

        /* Get a reverse iterator to one before the first element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator rend() const noexcept {
            return std::make_reverse_iterator(begin());
        }

        /* Get a reverse iterator to one before the first element in the tuple. */
        [[nodiscard]] constexpr const_reverse_iterator crend() const noexcept {
            return std::make_reverse_iterator(cbegin());
        }
    };

    template <typename... Ts>
    tuple_storage(Ts&&...) -> tuple_storage<meta::remove_rvalue<Ts>...>;

}


}



#endif  // BERTRAND_TUPLE_H