#ifndef BERTRAND_PYTHON_CORE_FUNC_H
#define BERTRAND_PYTHON_CORE_FUNC_H

#include "declarations.h"
#include "object.h"
#include "except.h"
#include "ops.h"
#include "access.h"


namespace py {


/////////////////////////
////    ARGUMENTS    ////
/////////////////////////


namespace impl {

    struct ArgKind {
        enum Flags : uint8_t {
            POS                 = 0b1,
            KW                  = 0b10,
            OPT                 = 0b100,
            VARIADIC            = 0b1000,
        } flags;

        constexpr ArgKind(uint8_t flags = 0) noexcept :
            flags(static_cast<Flags>(flags))
        {}
    
        constexpr operator uint8_t() const noexcept {
            return flags;
        }

        constexpr bool posonly() const noexcept {
            return (flags & ~OPT) == POS;
        }

        constexpr bool pos() const noexcept {
            return (flags & POS) & !(flags & VARIADIC);
        }

        constexpr bool args() const noexcept {
            return flags == (POS | VARIADIC);
        }

        constexpr bool kwonly() const noexcept {
            return (flags & ~OPT) == KW;
        }

        constexpr bool kw() const noexcept {
            return (flags & KW) & !(flags & VARIADIC);
        }

        constexpr bool kwargs() const noexcept {
            return flags == (KW | VARIADIC);
        }

        constexpr bool opt() const noexcept {
            return flags & OPT;
        }
    };

    template <typename T>
    struct OptionalArg {
        using type = T::type;
        using opt = OptionalArg;
        static constexpr StaticStr name = T::name;
        static constexpr ArgKind kind = T::kind | ArgKind::OPT;

        type value;

        [[nodiscard]] operator type() && {
            if constexpr (std::is_lvalue_reference_v<type>) {
                return value;
            } else {
                return std::move(value);
            }
        }
    };

    template <StaticStr Name, typename T>
    struct PositionalArg {
        using type = T;
        using opt = OptionalArg<PositionalArg>;
        static constexpr StaticStr name = Name;
        static constexpr ArgKind kind = ArgKind::POS;

        type value;

        [[nodiscard]] operator type() && {
            if constexpr (std::is_lvalue_reference_v<type>) {
                return value;
            } else {
                return std::move(value);
            }
        }
    };

    template <StaticStr Name, typename T>
    struct KeywordArg {
        using type = T;
        using opt = OptionalArg<KeywordArg>;
        static constexpr StaticStr name = Name;
        static constexpr ArgKind kind = ArgKind::KW;

        type value;

        [[nodiscard]] operator type() && {
            if constexpr (std::is_lvalue_reference_v<type>) {
                return value;
            } else {
                return std::move(value);
            }
        }
    };

    template <StaticStr Name, typename T>
    struct VarArgs {
        using type = std::conditional_t<
            std::is_rvalue_reference_v<T>,
            std::remove_reference_t<T>,
            std::conditional_t<
                std::is_lvalue_reference_v<T>,
                std::reference_wrapper<T>,
                T
            >
        >;
        static constexpr StaticStr name = Name;
        static constexpr ArgKind kind = ArgKind::POS | ArgKind::VARIADIC;

        std::vector<type> value;

        [[nodiscard]] operator std::vector<type>() && {
            return std::move(value);
        }
    };

    template <StaticStr Name, typename T>
    struct VarKwargs {
        using type = std::conditional_t<
            std::is_rvalue_reference_v<T>,
            std::remove_reference_t<T>,
            std::conditional_t<
                std::is_lvalue_reference_v<T>,
                std::reference_wrapper<T>,
                T
            >
        >;
        static constexpr StaticStr name = Name;
        static constexpr ArgKind kind = ArgKind::KW | ArgKind::VARIADIC;

        std::unordered_map<std::string, T> value;

        [[nodiscard]] operator std::unordered_map<std::string, T>() && {
            return std::move(value);
        }
    };

    /* A keyword parameter pack obtained by double-dereferencing a Python object. */
    template <mapping_like T>
    struct KwargPack {
        using key_type = T::key_type;
        using mapped_type = T::mapped_type;
        using type = mapped_type;
        static constexpr StaticStr name = "";
        static constexpr ArgKind kind = ArgKind::KW | ArgKind::VARIADIC;

        T value;

    private:

        static constexpr bool can_iterate =
            impl::yields_pairs_with<T, key_type, mapped_type> ||
            impl::has_items<T> ||
            (impl::has_keys<T> && impl::has_values<T>) ||
            (impl::yields<T, key_type> && impl::lookup_yields<T, mapped_type, key_type>) ||
            (impl::has_keys<T> && impl::lookup_yields<T, mapped_type, key_type>);

        auto transform() const {
            if constexpr (impl::yields_pairs_with<T, key_type, mapped_type>) {
                return value;

            } else if constexpr (impl::has_items<T>) {
                return value.items();

            } else if constexpr (impl::has_keys<T> && impl::has_values<T>) {
                return std::ranges::views::zip(value.keys(), value.values());

            } else if constexpr (
                impl::yields<T, key_type> && impl::lookup_yields<T, mapped_type, key_type>
            ) {
                return std::ranges::views::transform(
                    value,
                    [&](const key_type& key) {
                        return std::make_pair(key, value[key]);
                    }
                );

            } else {
                return std::ranges::views::transform(
                    value.keys(),
                    [&](const key_type& key) {
                        return std::make_pair(key, value[key]);
                    }
                );
            }
        }

    public:

        template <typename U = T> requires (can_iterate)
        auto begin() const { return std::ranges::begin(transform()); }
        template <typename U = T> requires (can_iterate)
        auto cbegin() const { return begin(); }
        template <typename U = T> requires (can_iterate)
        auto end() const { return std::ranges::end(transform()); }
        template <typename U = T> requires (can_iterate)
        auto cend() const { return end(); }
    };

    /* A positional parameter pack obtained by dereferencing a Python object. */
    template <iterable T>
    struct ArgPack {
        using type = iter_type<T>;
        static constexpr StaticStr name = "";
        static constexpr ArgKind kind = ArgKind::POS | ArgKind::VARIADIC;

        T value;

        auto begin() const { return std::ranges::begin(value); }
        auto cbegin() const { return begin(); }
        auto end() const { return std::ranges::end(value); }
        auto cend() const { return end(); }

        template <typename U = T> requires (impl::mapping_like<U>)
        auto operator*() const {
            return KwargPack<T>{std::forward<T>(value)};
        }
    };

}


/* A compile-time argument annotation that represents a bound positional or keyword
argument to a py::Function.  Uses aggregate initialization to extend the lifetime of
temporaries. */
template <StaticStr Name, typename T>
struct Arg {
    static_assert(Name != "", "Argument must have a name.");

    using type = T;
    using pos = impl::PositionalArg<Name, T>;
    using args = impl::VarArgs<Name, T>;
    using kw = impl::KeywordArg<Name, T>;
    using kwargs = impl::VarKwargs<Name, T>;
    using opt = impl::OptionalArg<Arg>;

    static constexpr StaticStr name = Name;
    static constexpr impl::ArgKind kind = impl::ArgKind::POS | impl::ArgKind::KW;

    type value;

    /* rvalue argument proxies are generated whenever a function is called.  Making
    them convertible to the underlying type means they can be used to call external
    C++ functions that are not aware of Python argument annotations. */
    [[nodiscard]] operator type() && {
        if constexpr (std::is_lvalue_reference_v<type>) {
            return value;
        } else {
            return std::move(value);
        }
    }
};


namespace impl {

    /* A singleton argument factory that allows arguments to be constructed via
    familiar assignment syntax, which extends the lifetime of temporaries. */
    template <StaticStr Name>
    struct ArgFactory {
        template <typename T>
        constexpr Arg<Name, T> operator=(T&& value) const {
            return {std::forward<T>(value)};
        }
    };

    /* Default seed for FNV-1a hash function. */
    constexpr size_t fnv1a_hash_seed = [] {
        if constexpr (sizeof(size_t) > 4) {
            return 14695981039346656037ULL;
        } else {
            return 2166136261u;
        }
    }();

    /* Default prime for FNV-1a hash function. */
    constexpr size_t fnv1a_hash_prime = [] {
        if constexpr (sizeof(size_t) > 4) {
            return 1099511628211ULL;
        } else {
            return 16777619u;
        }
    }();

    /* In the vast majority of cases, adjusting the seed is all that's needed to get a
    good FNV-1a hash, but just in case, we also provide the next 9 primes in case the
    default value cannot be used. */
    constexpr std::array<size_t, 10> fnv1a_fallback_primes = [] -> std::array<size_t, 10> {
        if constexpr (sizeof(size_t) > 4) {
            return {
                fnv1a_hash_prime,
                1099511628221ULL,
                1099511628227ULL,
                1099511628323ULL,
                1099511628329ULL,
                1099511628331ULL,
                1099511628359ULL,
                1099511628401ULL,
                1099511628403ULL,
                1099511628427ULL,
            };
        } else {
            return {
                fnv1a_hash_prime,
                16777633u,
                16777639u,
                16777643u,
                16777669u,
                16777679u,
                16777681u,
                16777699u,
                16777711u,
                16777721,
            };
        }
    }();

    /* A deterministic FNV-1a string hashing function that gives the same results at
    both compile time and run time. */
    constexpr size_t fnv1a(
        const char* str,
        size_t seed = fnv1a_hash_seed,
        size_t prime = fnv1a_hash_prime
    ) noexcept {
        while (*str) {
            seed ^= static_cast<size_t>(*str);
            seed *= prime;
            ++str;
        }
        return seed;
    }

    /* Round a number up to the next power of two unless it is one already. */
    template <std::unsigned_integral T>
    T next_power_of_two(T n) {
        constexpr size_t bits = sizeof(T) * 8;
        --n;
        for (size_t i = 1; i < bits; i <<= 1) {
            n |= (n >> i);
        }
        return ++n;
    }

    /// TODO: hash collisions should be exceedingly rare, but I can implement some
    /// minimal guards against them by wrapping a cached function call in a try-catch
    /// that catches TypeErrors and initiates a full search of the overload trie.  If
    /// the final node does not match the cached node, then I swallow the error and
    /// retry the call with the correct overload.

    /* A simple, trivially-destructible representation of a single parameter in a
    function signature or call site. */
    struct Param {
        std::string_view name;
        PyTypeObject* type;
        ArgKind kind;

        constexpr bool posonly() const noexcept { return kind.posonly(); }
        constexpr bool pos() const noexcept { return kind.pos(); }
        constexpr bool args() const noexcept { return kind.args(); }
        constexpr bool kwonly() const noexcept { return kind.kwonly(); }
        constexpr bool kw() const noexcept { return kind.kw(); }
        constexpr bool kwargs() const noexcept { return kind.kwargs(); }
        constexpr bool opt() const noexcept { return kind.opt(); }

        /* Parse a C++ string that represents an argument name, throwing an error if it
        is invalid. */
        static std::string_view get_name(std::string_view str) {
            std::string_view sub = str.substr(
                str.starts_with("*") +
                str.starts_with("**")
            );
            if (sub.empty()) {
                throw TypeError("argument name cannot be empty");
            } else if (std::isdigit(sub.front())) {
                throw TypeError(
                    "argument name cannot start with a number: '" +
                    std::string(sub) + "'"
                );
            }
            for (const char c : sub) {
                if (std::isalnum(c) || c == '_') {
                    continue;
                }
                throw TypeError(
                    "argument name must only contain alphanumerics and underscores: '" +
                    std::string(sub) + "'"
                );
            }
            return str;
        }

        /* Parse a Python string that represents an argument name, throwing an error if
        it is invalid. */
        static std::string_view get_name(PyObject* str) {
            Py_ssize_t len;
            const char* data = PyUnicode_AsUTF8AndSize(str, &len);
            if (data == nullptr) {
                Exception::from_python();
            }
            return get_name({data, static_cast<size_t>(len)});
        }

    };

    /* A read-only container of `Param` objects that also holds a combined hash
    suitable for cache optimization when searching a function's overload trie.  The
    underlying container type is flexible, and will generally be either a `std::array`
    (if the number of arguments is known ahead of time) or a `std::vector` (if they
    must be dynamic), but any container that supports read-only iteration, item access,
    and `size()` queries is supported. */
    template <yields<const Param&> T>
        requires (has_size<T> && lookup_yields<T, const Param&, size_t>)
    struct Params {
        T value;
        size_t hash;

        const Param& operator[](size_t i) const noexcept { return value[i]; }
        size_t size() const noexcept { return std::ranges::size(value); }
        bool empty() const noexcept { return std::ranges::empty(value); }
        auto begin() const noexcept { return std::ranges::begin(value); }
        auto cbegin() const noexcept { return std::ranges::cbegin(value); }
        auto end() const noexcept { return std::ranges::end(value); }
        auto cend() const noexcept { return std::ranges::cend(value); }
    };

    /* Inspect an annotated Python function and extract its inline type hints so that
    they can be translated into corresponding parameter lists. */
    struct Inspect {
    private:

        static Object import_typing() {
            PyObject* typing = PyImport_ImportModule("typing");
            if (typing == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(typing);
        }

        static Object import_types() {
            PyObject* types = PyImport_ImportModule("types");
            if (types == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(types);
        }

        Object get_signature() const {
            // signature = inspect.signature(func)
            // hints = typing.get_type_hints(func)
            // signature = signature.replace(
            //      return_annotation=hints.get("return", inspect.Parameter.empty),
            //      parameters=[
            //         p if p.annotation is inspect.Parameter.empty else
            //         p.replace(annotation=hints[p.name])
            //         for p in signature.parameters.values()
            //     ]
            // )
            Object signature = reinterpret_steal<Object>(PyObject_CallOneArg(
                ptr(getattr<"signature">(inspect)),
                ptr(func)
            ));
            if (signature.is(nullptr)) {
                Exception::from_python();
            }
            PyObject* get_type_hints_args[] = {ptr(func), Py_True};
            Object get_type_hints_kwnames = reinterpret_steal<Object>(
                PyTuple_Pack(1, TemplateString<"include_extras">::ptr)
            );
            Object hints = reinterpret_steal<Object>(PyObject_Vectorcall(
                ptr(getattr<"get_type_hints">(typing)),
                get_type_hints_args,
                2,
                ptr(get_type_hints_kwnames)
            ));
            if (hints.is(nullptr)) {
                Exception::from_python();
            }
            Object empty = getattr<"empty">(getattr<"Parameter">(inspect));
            Object parameters = reinterpret_steal<Object>(PyObject_CallMethodNoArgs(
                ptr(getattr<"parameters">(signature)),
                TemplateString<"values">::ptr
            ));
            Object new_params = reinterpret_steal<Object>(
                PyList_New(len(parameters))
            );
            Py_ssize_t idx = 0;
            for (Object param : parameters) {
                Object annotation = getattr<"annotation">(param);
                if (!annotation.is(empty)) {
                    annotation = reinterpret_steal<Object>(PyDict_GetItemWithError(
                        ptr(hints),
                        ptr(getattr<"name">(param))
                    ));
                    if (annotation.is(nullptr)) {
                        if (PyErr_Occurred()) {
                            Exception::from_python();
                        } else {
                            throw KeyError(
                                "no type hint for parameter: " + repr(param)
                            );
                        }
                    }
                    PyObject* replace_args[] = {ptr(annotation)};
                    Object replace_kwnames = reinterpret_steal<Object>(PyTuple_Pack(
                        1,
                        TemplateString<"annotation">::ptr
                    ));
                    if (replace_kwnames.is(nullptr)) {
                        Exception::from_python();
                    }
                    param = reinterpret_steal<Object>(PyObject_Vectorcall(
                        ptr(getattr<"replace">(param)),
                        replace_args,
                        1,
                        ptr(replace_kwnames)
                    ));
                    if (param.is(nullptr)) {
                        Exception::from_python();
                    }
                }
                // steals a reference
                PyList_SET_ITEM(ptr(new_params), idx++, release(param));
            }
            Object return_annotation = reinterpret_steal<Object>(PyDict_GetItem(
                ptr(hints),
                TemplateString<"return">::ptr
            ));
            if (return_annotation.is(nullptr)) {
                return_annotation = empty;
            }
            PyObject* replace_args[] = {
                ptr(return_annotation),
                ptr(new_params)
            };
            Object replace_kwnames = reinterpret_steal<Object>(PyTuple_Pack(
                2,
                TemplateString<"return_annotation">::ptr,
                TemplateString<"parameters">::ptr
            ));
            if (replace_kwnames.is(nullptr)) {
                Exception::from_python();
            }
            signature = reinterpret_steal<Object>(PyObject_Vectorcall(
                ptr(getattr<"replace">(signature)),
                replace_args,
                2,
                ptr(replace_kwnames)
            ));
            if (signature.is(nullptr)) {
                Exception::from_python();
            }
            return signature;
        }

    public:
        Object func;
        Object signature;
        size_t seed;
        size_t prime;

        explicit Inspect(
            PyObject* func,
            size_t seed = fnv1a_hash_seed,
            size_t prime = fnv1a_hash_prime
        ) : func(reinterpret_borrow<Object>(func)),
            signature(get_signature()),
            seed(seed),
            prime(prime)
        {}

        Inspect(const Inspect& other) = delete;
        Inspect(Inspect&& other) = delete;
        Inspect& operator=(const Inspect& other) = delete;
        Inspect& operator=(Inspect&& other) noexcept = delete;

        /* A callback function to use when parsing inline type hints within a Python
        function declaration. */
        struct Callback {
            using Func = std::function<bool(Object, std::vector<PyTypeObject*>& result)>;
            std::string id;
            Func callback;
        };

        /* Initiate a search of the callback map in order to parse a Python-style type
        hint.  The search stops at the first callback that returns true, otherwise the
        hint is interpreted as either a single type if it is a Python class, or a
        generic `object` type otherwise. */
        static void parse(Object hint, std::vector<PyTypeObject*>& out) {
            for (const Callback& cb : callbacks) {
                if (cb.callback(hint, out)) {
                    return;
                }
            }

            // raw types are forwarded directly
            if (PyType_Check(ptr(hint))) {
                out.push_back(reinterpret_cast<PyTypeObject*>(ptr(hint)));
                return;
            }

            // Annotated types are unwrapped and reprocessed if not handled by a callback
            Object typing = import_typing();
            Object origin = reinterpret_steal<Object>(PyObject_CallOneArg(
                ptr(getattr<"get_origin">(typing)),
                ptr(hint)
            ));
            if (origin.is(nullptr)) {
                Exception::from_python();
            } else if (origin.is(getattr<"Annotated">(typing))) {
                Object args = reinterpret_steal<Object>(PyObject_CallOneArg(
                    ptr(getattr<"get_args">(typing)),
                    ptr(hint)
                ));
                if (args.is(nullptr)) {
                    Exception::from_python();
                }
                parse(reinterpret_borrow<Object>(
                    PyTuple_GET_ITEM(ptr(args), 0)
                ), out);
                return;
            }

            // unrecognized hints are treated as Any
            out.push_back(&PyBaseObject_Type);
        }

        /* In order to provide custom handlers for Python type hints, each annotation
        will be passed through a series of callbacks that convert it into a flat list
        of Python types, which will be used to generate the final overload keys.

        Each callback is tested in order and expected to return true if it can handle
        the hint, in which case the search terminates and the final state of the `out`
        vector will be pushed into the set of possible overload keys.  If no callback
        can handle a given hint, then it is interpreted as a single type if it is a
        Python class, or as a generic `object` type otherwise, which is equivalent to
        Python's `typing.Any`.  Some type hints, such as `Union` and `Optional`, will
        recursively search the callback map in order to split the hint into its
        constituent types, which will be registered as unique overloads.

        Note that `inspect.get_type_hints(include_extras=True)` is used to extract the
        type hints from the function signature, meaning that stringized annotations and
        forward references will be normalized before any callbacks are invoked.  The
        `include_extras` flag is used to ensure that `typing.Annotated` hints are
        preserved, so that they can be interpreted by the callback map if necessary.
        The default behavior in this case is to simply extract the underlying type,
        but custom callbacks can be added to interpret these annotations as needed.

        For performance reasons, the types that are added to the `out` vector are
        always expected to be BORROWED references, and do not own the underlying
        type objects.  This allows the overload keys to be trivially destructible,
        which avoids an extra loop in their destructors.  Since an overload key is
        created every time a function is called, this is significant. */
        inline static std::vector<Callback> callbacks {
            /// NOTE: Callbacks are linearly searched, so more common constructs should
            /// be generally placed at the front of the list for performance reasons.
            {
                /// TODO: handling GenericAlias types is going to be fairly complicated, 
                /// and will require interactions with the global type map, and thus a
                /// forward declaration here.
                "types.GenericAlias",
                [](Object hint, std::vector<PyTypeObject*>& out) -> bool {
                    Object types = import_types();
                    if (isinstance(hint, getattr<"GenericAlias">(types))) {
                        Object typing = import_typing();
                        Object origin = reinterpret_steal<Object>(PyObject_CallOneArg(
                            ptr(getattr<"get_origin">(typing)),
                            ptr(hint)
                        ));
                        /// TODO: search in type map or fall back to Object
                        Object args = reinterpret_steal<Object>(PyObject_CallOneArg(
                            ptr(getattr<"get_args">(typing)),
                            ptr(hint)
                        ));
                        /// TODO: parametrize the bertrand type with the same args.  If
                        /// this causes a template error, then fall back to its default
                        /// specialization (i.e. list[Object]).
                        throw NotImplementedError(
                            "generic type subscription is not yet implemented"
                        );
                        return true;
                    }
                    return false;
                }
            },
            {
                "types.UnionType",
                [](Object hint, std::vector<PyTypeObject*>& out) -> bool {
                    Object types = import_types();
                    if (isinstance(hint, getattr<"UnionType">(types))) {
                        Object args = reinterpret_steal<Object>(PyObject_CallOneArg(
                            ptr(getattr<"get_args">(types)),
                            ptr(hint)
                        ));
                        Py_ssize_t len = PyTuple_GET_SIZE(ptr(args));
                        for (Py_ssize_t i = 0; i < len; ++i) {
                            parse(reinterpret_borrow<Object>(
                                PyTuple_GET_ITEM(ptr(args), i)
                            ), out);
                        }
                        return true;
                    }
                    return false;
                }
            },
            {
                /// NOTE: when `typing.get_origin()` is called on a `typing.Optional`,
                /// it returns `typing.Union`, meaning that this handler will also
                /// implicitly cover `Optional` annotations for free.
                "typing.Union",
                [](Object hint, std::vector<PyTypeObject*>& out) -> bool {
                    Object typing = import_typing();
                    Object origin = reinterpret_steal<Object>(PyObject_CallOneArg(
                        ptr(getattr<"get_origin">(typing)),
                        ptr(hint)
                    ));
                    if (origin.is(nullptr)) {
                        Exception::from_python();
                    } else if (origin.is(getattr<"Union">(typing))) {
                        Object args = reinterpret_steal<Object>(PyObject_CallOneArg(
                            ptr(getattr<"get_args">(typing)),
                            ptr(hint)
                        ));
                        Py_ssize_t len = PyTuple_GET_SIZE(ptr(args));
                        for (Py_ssize_t i = 0; i < len; ++i) {
                            parse(reinterpret_borrow<Object>(
                                PyTuple_GET_ITEM(ptr(args), i)
                            ), out);
                        }
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.Any",
                [](Object hint, std::vector<PyTypeObject*>& out) -> bool {
                    Object typing = import_typing();
                    Object origin = reinterpret_steal<Object>(PyObject_CallOneArg(
                        ptr(getattr<"get_origin">(typing)),
                        ptr(hint)
                    ));
                    if (origin.is(nullptr)) {
                        Exception::from_python();
                    } else if (origin.is(getattr<"Any">(typing))) {
                        out.push_back(&PyBaseObject_Type);
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.TypeAliasType",
                [](Object hint, std::vector<PyTypeObject*>& out) -> bool {
                    Object typing = import_typing();
                    if (isinstance(hint, getattr<"TypeAliasType">(typing))) {
                        parse(getattr<"__value__">(hint), out);
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.Literal",
                [](Object hint, std::vector<PyTypeObject*>& out) -> bool {
                    Object typing = import_typing();
                    Object origin = reinterpret_steal<Object>(PyObject_CallOneArg(
                        ptr(getattr<"get_origin">(typing)),
                        ptr(hint)
                    ));
                    if (origin.is(nullptr)) {
                        Exception::from_python();
                    } else if (origin.is(getattr<"Literal">(typing))) {
                        Object args = reinterpret_steal<Object>(PyObject_CallOneArg(
                            ptr(getattr<"get_args">(typing)),
                            ptr(hint)
                        ));
                        if (args.is(nullptr)) {
                            Exception::from_python();
                        }
                        Py_ssize_t len = PyTuple_GET_SIZE(ptr(args));
                        for (Py_ssize_t i = 0; i < len; ++i) {
                            PyTypeObject* type = Py_TYPE(PyTuple_GET_ITEM(ptr(args), i));
                            bool contains = false;
                            for (PyTypeObject* t : out) {
                                if (t == type) {
                                    contains = true;
                                }
                            }
                            if (!contains) {
                                out.push_back(type);
                            }
                        }
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.LiteralString",
                [](Object hint, std::vector<PyTypeObject*>& out) -> bool {
                    Object typing = import_typing();
                    if (hint.is(getattr<"LiteralString">(typing))) {
                        out.push_back(&PyUnicode_Type);
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.AnyStr",
                [](Object hint, std::vector<PyTypeObject*>& out) -> bool {
                    Object typing = import_typing();
                    if (hint.is(getattr<"AnyStr">(typing))) {
                        out.push_back(&PyUnicode_Type);
                        out.push_back(&PyBytes_Type);
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.NoReturn",
                [](Object hint, std::vector<PyTypeObject*>& out) -> bool {
                    Object typing = import_typing();
                    if (
                        hint.is(getattr<"NoReturn">(typing)) ||
                        hint.is(getattr<"Never">(typing))
                    ) {
                        /// NOTE: this handler models NoReturn/Never by not pushing a
                        /// type to the `out` vector, giving an empty return type.
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.TypeGuard",
                [](Object hint, std::vector<PyTypeObject*>& out) -> bool {
                    Object typing = import_typing();
                    Object origin = reinterpret_steal<Object>(PyObject_CallOneArg(
                        ptr(getattr<"get_origin">(typing)),
                        ptr(hint)
                    ));
                    if (origin.is(nullptr)) {
                        Exception::from_python();
                    } else if (origin.is(getattr<"TypeGuard">(typing))) {
                        out.push_back(&PyBool_Type);
                        return true;
                    }
                    return false;
                }
            }
        };

        /* Get the possible return types of the function, using the same callback
        handlers as the parameters.  Note that functions with `typing.NoReturn` or
        `typing.Never` annotations can return an empty vector. */
        std::vector<PyTypeObject*> returns() const {
            Object return_annotation = getattr<"return_annotation">(signature);
            std::vector<PyTypeObject*> keys;
            parse(return_annotation, keys);
            return keys;
        }

        auto begin() const {
            if (overload_keys.empty()) {
                get_overloads();
            }
            return overload_keys.cbegin();
        }
        auto cbegin() const { return begin(); }
        auto end() const { return overload_keys.cend(); }
        auto cend() const { return end(); }

    private:
        Object inspect = [] {
            PyObject* inspect = PyImport_Import(TemplateString<"inspect">::ptr);
            if (inspect == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(inspect);
        }();
        Object typing = [] {
            PyObject* inspect = PyImport_Import(TemplateString<"typing">::ptr);
            if (inspect == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(inspect);
        }();

        using Params = impl::Params<std::vector<Param>>;
        mutable std::vector<Params> overload_keys;

        /* Iterate over the Python signature and invoke the matching callbacks */
        void get_overloads() const {
            Object Parameter = getattr<"Parameter">(inspect);
            Object empty = getattr<"empty">(Parameter);
            Object POSITIONAL_ONLY = getattr<"POSITIONAL_ONLY">(Parameter);
            Object POSITIONAL_OR_KEYWORD = getattr<"POSITIONAL_OR_KEYWORD">(Parameter);
            Object VAR_POSITIONAL = getattr<"VAR_POSITIONAL">(Parameter);
            Object KEYWORD_ONLY = getattr<"KEYWORD_ONLY">(Parameter);
            Object VAR_KEYWORD = getattr<"VAR_KEYWORD">(Parameter);

            Object parameters = getattr<"parameters">(signature);
            overload_keys.push_back({std::vector<Param>{}, 0});
            overload_keys.back().value.reserve(len(parameters));
            for (Object param : parameters) {
                // determine the name and category of each `inspect.Parameter` object
                std::string_view name = Param::get_name(
                    ptr(getattr<"name">(param)
                ));
                ArgKind category;
                Object kind = getattr<"kind">(param);
                if (kind.is(POSITIONAL_ONLY)) {
                    category = getattr<"default">(param).is(empty) ?
                        ArgKind::POS :
                        ArgKind::POS | ArgKind::OPT;
                } else if (kind.is(POSITIONAL_OR_KEYWORD)) {
                    category = getattr<"default">(param).is(empty) ?
                        ArgKind::POS | ArgKind::KW :
                        ArgKind::POS | ArgKind::KW | ArgKind::OPT;
                } else if (kind.is(KEYWORD_ONLY)) {
                    category = getattr<"default">(param).is(empty) ?
                        ArgKind::KW :
                        ArgKind::KW | ArgKind::OPT;
                } else if (kind.is(VAR_POSITIONAL)) {
                    category = ArgKind::POS | ArgKind::VARIADIC;
                } else if (kind.is(VAR_KEYWORD)) {
                    category = ArgKind::KW | ArgKind::VARIADIC;
                } else {
                    throw TypeError("unrecognized parameter kind: " + repr(kind));
                }

                // parse the annotation for each `inspect.Parameter` object
                std::vector<PyTypeObject*> types;
                parse(param, types);

                // if there is more than one type in the output vector, then the
                // existing keys must be duplicated to maintain uniqueness
                overload_keys.reserve(types.size() * overload_keys.size());
                for (size_t i = 1; i < types.size(); ++i) {
                    for (size_t j = 0; j < overload_keys.size(); ++j) {
                        auto&& key = overload_keys[j];
                        overload_keys.push_back(key);
                        overload_keys.back().value.reserve(key.value.capacity());
                    }
                }

                // append the types to the overload keys and update their hashes such
                // that each gives a unique path through a function's overload trie
                for (size_t i = 0; i < types.size(); ++i) {
                    PyTypeObject* type = types[i];
                    for (size_t j = 0; j < overload_keys.size(); ++j) {
                        Params& key = overload_keys[i * overload_keys.size() + j];
                        key.value.push_back({
                            name,
                            type,
                            category
                        });
                        key.hash = hash_combine(
                            key.hash,
                            fnv1a(name.data(), seed, prime),
                            reinterpret_cast<size_t>(type)
                        );
                    }
                }
            }
        }

    };

    template <typename T>
    static constexpr bool _is_arg = false;
    template <typename T>
    static constexpr bool _is_arg<OptionalArg<T>> = true;
    template <StaticStr Name, typename T>
    static constexpr bool _is_arg<PositionalArg<Name, T>> = true;
    template <StaticStr Name, typename T>
    static constexpr bool _is_arg<Arg<Name, T>> = true;
    template <StaticStr Name, typename T>
    static constexpr bool _is_arg<KeywordArg<Name, T>> = true;
    template <StaticStr Name, typename T>
    static constexpr bool _is_arg<VarArgs<Name, T>> = true;
    template <StaticStr Name, typename T>
    static constexpr bool _is_arg<VarKwargs<Name, T>> = true;
    template <typename T>
    static constexpr bool _is_arg<ArgPack<T>> = true;
    template <typename T>
    static constexpr bool _is_arg<KwargPack<T>> = true;
    template <typename T>
    concept is_arg = _is_arg<std::remove_cvref_t<T>>;

    /* Inspect a C++ argument at compile time.  Normalizes unannotated types to
    positional-only arguments to maintain C++ style. */
    template <typename T>
    struct ArgTraits {
        using type                                  = T;
        using no_opt                                = T;
        static constexpr StaticStr name             = "";
        static constexpr ArgKind kind               = ArgKind::POS;
        static constexpr bool posonly() noexcept    { return kind.posonly(); }
        static constexpr bool pos() noexcept        { return kind.pos(); }
        static constexpr bool args() noexcept       { return kind.args(); }
        static constexpr bool kwonly() noexcept     { return kind.kwonly(); }
        static constexpr bool kw() noexcept         { return kind.kw(); }
        static constexpr bool kwargs() noexcept     { return kind.kwargs(); }
        static constexpr bool opt() noexcept        { return kind.opt(); }
    };

    /* Inspect a C++ argument at compile time.  Forwards to the annotated type's
    interface where possible. */
    template <is_arg T>
    struct ArgTraits<T> {
    private:

        template <typename U>
        struct _no_opt { using type = U; };
        template <typename U>
        struct _no_opt<OptionalArg<U>> { using type = U; };

    public:
        using type                                  = std::remove_cvref_t<T>::type;
        using no_opt                                = _no_opt<std::remove_cvref_t<T>>::type;
        static constexpr StaticStr name             = std::remove_cvref_t<T>::name;
        static constexpr ArgKind kind               = std::remove_cvref_t<T>::kind;
        static constexpr bool posonly() noexcept    { return kind.posonly(); }
        static constexpr bool pos() noexcept        { return kind.pos(); }
        static constexpr bool args() noexcept       { return kind.args(); }
        static constexpr bool kwonly() noexcept     { return kind.kwonly(); }
        static constexpr bool kw() noexcept         { return kind.kw(); }
        static constexpr bool kwargs() noexcept     { return kind.kwargs(); }
        static constexpr bool opt() noexcept        { return kind.opt(); }
    };

    /* Inspect an annotated C++ parameter list at compile time and extract metadata
    that allows a corresponding function to be called with Python-style arguments from
    C++. */
    template <typename... Args>
    struct Arguments : BertrandTag {
    private:

        template <size_t I, typename... Ts>
        static consteval size_t _n_posonly() { return 0; }
        template <size_t I, typename T, typename... Ts>
        static consteval size_t _n_posonly() {
            return _n_posonly<I + ArgTraits<T>::posonly(), Ts...>();
        }

        template <size_t I, typename... Ts>
        static consteval size_t _n_pos() { return 0; }
        template <size_t I, typename T, typename... Ts>
        static consteval size_t _n_pos() {
            return _n_pos<I + ArgTraits<T>::pos(), Ts...>();
        }

        template <size_t I, typename... Ts>
        static consteval size_t _n_kw() { return 0; }
        template <size_t I, typename T, typename... Ts>
        static consteval size_t _n_kw() {
            return _n_kw<I + ArgTraits<T>::kw(), Ts...>();
        }

        template <size_t I, typename... Ts>
        static consteval size_t _n_kwonly() { return 0; }
        template <size_t I, typename T, typename... Ts>
        static consteval size_t _n_kwonly() {
            return _n_kwonly<I + ArgTraits<T>::kwonly(), Ts...>();
        }

        template <size_t I, typename... Ts>
        static consteval size_t _n_opt() { return 0; }
        template <size_t I, typename T, typename... Ts>
        static consteval size_t _n_opt() {
            return _n_opt<I + ArgTraits<T>::opt(), Ts...>();
        }

        template <size_t I, typename... Ts>
        static consteval size_t _n_opt_posonly() { return 0; }
        template <size_t I, typename T, typename... Ts>
        static consteval size_t _n_opt_posonly() {
            return _n_opt_posonly<
                I + (ArgTraits<T>::posonly() && ArgTraits<T>::opt()),
                Ts...
            >();
        }

        template <size_t I, typename... Ts>
        static consteval size_t _n_opt_pos() { return 0; }
        template <size_t I, typename T, typename... Ts>
        static consteval size_t _n_opt_pos() {
            return _n_opt_pos<
                I + (ArgTraits<T>::pos() && ArgTraits<T>::opt()),
                Ts...
            >();
        }

        template <size_t I, typename... Ts>
        static consteval size_t _n_opt_kw() { return 0; }
        template <size_t I, typename T, typename... Ts>
        static consteval size_t _n_opt_kw() {
            return _n_opt_kw<
                I + (ArgTraits<T>::kw() && ArgTraits<T>::opt()),
                Ts...
            >();
        }

        template <size_t I, typename... Ts>
        static consteval size_t _n_opt_kwonly() { return 0; }
        template <size_t I, typename T, typename... Ts>
        static consteval size_t _n_opt_kwonly() {
            return _n_opt_kwonly<
                I + (ArgTraits<T>::kwonly() && ArgTraits<T>::opt()),
                Ts...
            >();
        }

        template <StaticStr Name, typename... Ts>
        static consteval size_t _idx() { return 0; }
        template <StaticStr Name, typename T, typename... Ts>
        static consteval size_t _idx() {
            return (ArgTraits<T>::name == Name) ? 0 : 1 + _idx<Name, Ts...>();
        }

        template <typename... Ts>
        static consteval size_t _args_idx() { return 0; }
        template <typename T, typename... Ts>
        static consteval size_t _args_idx() {
            return ArgTraits<T>::args() ? 0 : 1 + _args_idx<Ts...>();
        }

        template <typename... Ts>
        static consteval size_t _kw_idx() { return 0; }
        template <typename T, typename... Ts>
        static consteval size_t _kw_idx() {
            return ArgTraits<T>::kw() ? 0 : 1 + _kw_idx<Ts...>();
        }

        template <typename... Ts>
        static consteval size_t _kwonly_idx() { return 0; }
        template <typename T, typename... Ts>
        static consteval size_t _kwonly_idx() {
            return ArgTraits<T>::kwonly() ? 0 : 1 + _kwonly_idx<Ts...>();
        }

        template <typename... Ts>
        static consteval size_t _kwargs_idx() { return 0; }
        template <typename T, typename... Ts>
        static consteval size_t _kwargs_idx() {
            return ArgTraits<T>::kwargs() ? 0 : 1 + _kwargs_idx<Ts...>();
        }

        template <typename... Ts>
        static consteval size_t _opt_idx() { return 0; }
        template <typename T, typename... Ts>
        static consteval size_t _opt_idx() {
            return ArgTraits<T>::opt() ? 0 : 1 + _opt_idx<Ts...>();
        }

        template <typename... Ts>
        static consteval size_t _opt_posonly_idx() { return 0; }
        template <typename T, typename... Ts>
        static consteval size_t _opt_posonly_idx() {
            return ArgTraits<T>::posonly() && ArgTraits<T>::opt() ?
                0 : 1 + _opt_posonly_idx<Ts...>();
        }

        template <typename... Ts>
        static consteval size_t _opt_pos_idx() { return 0; }
        template <typename T, typename... Ts>
        static consteval size_t _opt_pos_idx() {
            return ArgTraits<T>::pos() && ArgTraits<T>::opt() ?
                0 : 1 + _opt_pos_idx<Ts...>();
        }

        template <typename... Ts>
        static consteval size_t _opt_kw_idx() { return 0; }
        template <typename T, typename... Ts>
        static consteval size_t _opt_kw_idx() {
            return ArgTraits<T>::kw() && ArgTraits<T>::opt() ?
                0 : 1 + _opt_kw_idx<Ts...>();
        }

        template <typename... Ts>
        static consteval size_t _opt_kwonly_idx() { return 0; }
        template <typename T, typename... Ts>
        static consteval size_t _opt_kwonly_idx() {
            return ArgTraits<T>::kwonly() && ArgTraits<T>::opt() ?
                0 : 1 + _opt_kwonly_idx<Ts...>();
        }

        template <size_t I, typename... Ts>
        static consteval bool _proper_argument_order() { return true; }
        template <size_t I, typename T, typename... Ts>
        static consteval bool _proper_argument_order() {
            if constexpr (
                (ArgTraits<T>::posonly() && (I > kw_idx || I > args_idx || I > kwargs_idx)) ||
                (ArgTraits<T>::pos() && (I > args_idx || I > kwonly_idx || I > kwargs_idx)) ||
                (ArgTraits<T>::args() && (I > kwonly_idx || I > kwargs_idx)) ||
                (ArgTraits<T>::kwonly() && (I > kwargs_idx))
            ) {
                return false;
            }
            return _proper_argument_order<I + 1, Ts...>();
        }

        template <size_t I, typename... Ts>
        static consteval bool _no_duplicate_arguments() { return true; }
        template <size_t I, typename T, typename... Ts>
        static consteval bool _no_duplicate_arguments() {
            if constexpr (
                (T::name != "" && I != idx<ArgTraits<T>::name>) ||
                (ArgTraits<T>::args() && I != args_idx) ||
                (ArgTraits<T>::kwargs() && I != kwargs_idx)
            ) {
                return false;
            }
            return _no_duplicate_arguments<I + 1, Ts...>();
        }

        template <size_t I, typename... Ts>
        static consteval bool _no_required_after_default() { return true; }
        template <size_t I, typename T, typename... Ts>
        static consteval bool _no_required_after_default() {
            if constexpr (ArgTraits<T>::pos() && !ArgTraits<T>::opt() && I > opt_idx) {
                return false;
            } else {
                return _no_required_after_default<I + 1, Ts...>();
            }
        }

        /* Get a one-hot encoded bitmask with the bit at index I set if and only if
        the parameter at that index is a required positional or keyword argument.
        Otherwise, return a zero mask. */
        template <size_t I>
        static consteval uint64_t build_mask() {
            if constexpr (
                ArgTraits<unpack_type<I, Args...>>::opt() ||
                ArgTraits<unpack_type<I, Args...>>::args() ||
                ArgTraits<unpack_type<I, Args...>>::kwargs()
            ) {
                return 0ULL;
            } else {
                return 1ULL << I;
            }
        }

    public:
        static constexpr size_t n                   = sizeof...(Args);
        static constexpr size_t n_posonly           = _n_posonly<0, Args...>();
        static constexpr size_t n_pos               = _n_pos<0, Args...>();
        static constexpr size_t n_kw                = _n_kw<0, Args...>();
        static constexpr size_t n_kwonly            = _n_kwonly<0, Args...>();
        static constexpr size_t n_opt               = _n_opt<0, Args...>();
        static constexpr size_t n_opt_posonly       = _n_opt_posonly<0, Args...>();
        static constexpr size_t n_opt_pos           = _n_opt_pos<0, Args...>();
        static constexpr size_t n_opt_kw            = _n_opt_kw<0, Args...>();
        static constexpr size_t n_opt_kwonly        = _n_opt_kwonly<0, Args...>();

        template <StaticStr Name>
        static constexpr bool has                   = _idx<Name, Args...>() != n;
        static constexpr bool has_posonly           = n_posonly > 0;
        static constexpr bool has_pos               = n_pos > 0;
        static constexpr bool has_kw                = n_kw > 0;
        static constexpr bool has_kwonly            = n_kwonly > 0;
        static constexpr bool has_opt               = n_opt > 0;
        static constexpr bool has_opt_posonly       = n_opt_posonly > 0;
        static constexpr bool has_opt_pos           = n_opt_pos > 0;
        static constexpr bool has_opt_kw            = n_opt_kw > 0;
        static constexpr bool has_opt_kwonly        = n_opt_kwonly > 0;
        static constexpr bool has_args              = _args_idx<Args...>() != n;
        static constexpr bool has_kwargs            = _kwargs_idx<Args...>() != n;

        template <StaticStr Name> requires (has<Name>)
        static constexpr size_t idx                 = _idx<Name, Args...>();
        static constexpr size_t kw_idx              = _kw_idx<Args...>();
        static constexpr size_t kwonly_idx          = _kwonly_idx<Args...>();
        static constexpr size_t opt_idx             = _opt_idx<Args...>();
        static constexpr size_t opt_posonly_idx     = _opt_posonly_idx<Args...>();
        static constexpr size_t opt_pos_idx         = _opt_pos_idx<Args...>();
        static constexpr size_t opt_kw_idx          = _opt_kw_idx<Args...>();
        static constexpr size_t opt_kwonly_idx      = _opt_kwonly_idx<Args...>();
        static constexpr size_t args_idx            = _args_idx<Args...>();
        static constexpr size_t kwargs_idx          = _kwargs_idx<Args...>();

        static constexpr bool args_are_convertible_to_python =
            (std::convertible_to<typename ArgTraits<Args>::type, Object> && ...);
        static constexpr bool proper_argument_order =
            _proper_argument_order<0, Args...>();
        static constexpr bool no_duplicate_arguments =
            _no_duplicate_arguments<0, Args...>();
        static constexpr bool no_required_after_default =
            _no_required_after_default<0, Args...>();

        template <size_t I>
        static constexpr bool is_positional = I < kwonly_idx && I < kwargs_idx;
        template <size_t I>
        static constexpr bool is_keyword = I >= kw_idx && I != args_idx;

        template <size_t I> requires (I < n)
        using at = unpack_type<I, Args...>;
        template <size_t I> requires (is_positional<I>)
        using Arg = ArgTraits<at<I>>::type;
        template <StaticStr Name> requires (has<Name> && is_keyword<idx<Name>>)
        using Kwarg = ArgTraits<at<idx<Name>>>::type;

        /* A single entry in a callback table, storing the argument name, a bitmask
        marking the argument as required/optional, a function that can be used to
        validate the argument, and a lazy function that can be used to retrieve its
        Python type object. */
        struct Callback {
            std::string_view name;
            uint64_t mask = 0;
            bool(*func)(PyTypeObject*) = nullptr;
            PyTypeObject*(*type)() = nullptr;
            explicit operator bool() const noexcept { return func != nullptr; }
            bool operator()(PyTypeObject* type) const { return func(type); }
        };

        /* A bitmask with a 1 in the position of all of the required arguments in the
        parameter list.  Each callback stores a one-hot encoded version of this mask in
        addition to the argument name and validation function.  This is just the union
        of each submask.  When the function is called, a corresponding mask will be
        built from scratch as each argument is parsed by OR-ing the submasks of all
        handled arguments.  After the argument list is complete, any missing arguments
        can be detected by doing an equality check against this mask.  If that
        evaluates to false, then further bitwise inspection can be done to determine
        exactly which arguments were missing, as well as their names.

        Note that this mask effectively limits the number of arguments that a function
        can accept to 64, which is a reasonable limit for most functions.  The
        performance benefits justify the limitation, and if you need more than 64
        arguments, you should probably be using a different design pattern. */
        static constexpr uint64_t required = [] {
            return []<size_t... Is>(std::index_sequence<Is...>) {
                return (build_mask<Is>() | ...);
            }(std::make_index_sequence<n>{});
        }();

    private:
        static constexpr Callback null_callback;

        /* Populate the positional argument table with an appropriate callback for
        each argument in the parameter list. */
        template <size_t I>
        static consteval void populate_positional_table(std::array<Callback, n>& table) {
            table[I] = {
                ArgTraits<at<I>>::name,
                build_mask<I>(),
                [](PyTypeObject* type) -> bool {
                    using T = typename ArgTraits<at<I>>::type;
                    if constexpr (has_type<T>) {
                        int rc = PyObject_IsSubclass(
                            reinterpret_cast<PyObject*>(type),
                            ptr(Type<T>())
                        );
                        if (rc < 0) {
                            Exception::from_python();
                        }
                        return rc;
                    } else {
                        throw TypeError(
                            "C++ type has no Python equivalent: " +
                            demangle(typeid(T).name())
                        );
                    }
                },
                []() -> PyTypeObject* {
                    using T = typename ArgTraits<at<I>>::type;
                    if constexpr (has_type<T>) {
                        return reinterpret_cast<PyTypeObject*>(ptr(Type<T>()));
                    } else {
                        throw TypeError(
                            "C++ type has no Python equivalent: " +
                            demangle(typeid(T).name())
                        );
                    }
                }
            };
        }

        /* An array of positional arguments to callbacks and bitmasks that can be used
        to validate the argument dynamically at runtime. */
        static constexpr std::array<Callback, n> positional_table = [] {
            std::array<Callback, n> table;
            []<size_t... Is>(std::index_sequence<Is...>, std::array<Callback, n>& table) {
                (populate_positional_table<Is>(table), ...);
            }(std::make_index_sequence<n>{}, table);
            return table;
        }();

        /* Get the size of the perfect hash table required to efficiently validate
        keyword arguments as a power of 2. */
        static consteval size_t keyword_table_size() {
            return next_power_of_two(2 * n_kw);
        }

        /* Take the modulus with respect to the keyword table by exploiting the power
        of 2 table size. */
        static constexpr size_t keyword_table_index(size_t hash) {
            return hash & (keyword_table_size() - 1);
        }

        /* Given a precomputed keyword index into the hash table, check to see if any
        subsequent arguments in the parameter list hash to the same index. */
        template <size_t I>
        static consteval bool collides_recursive(size_t idx, size_t seed, size_t prime) {
            if constexpr (I < sizeof...(Args)) {
                if constexpr (ArgTraits<at<I>>::kw) {
                    size_t hash = fnv1a(
                        ArgTraits<at<I>>::name,
                        seed,
                        prime
                    );
                    return
                        (keyword_table_index(hash) == idx) ||
                        collides_recursive<I + 1>(idx, seed, prime);
                } else {
                    return collides_recursive<I + 1>(idx, seed, prime);
                }
            } else {
                return false;
            }
        };

        /* Check to see if the candidate seed and prime produce any collisions for the
        target argument at index I. */
        template <size_t I>
        static consteval bool collides(size_t seed, size_t prime) {
            if constexpr (I < sizeof...(Args)) {
                if constexpr (ArgTraits<at<I>>::kw) {
                    size_t hash = fnv1a(
                        ArgTraits<at<I>>::name,
                        seed,
                        prime
                    );
                    return collides_recursive<I + 1>(
                        keyword_table_index(hash),
                        seed,
                        prime
                    ) || collides<I + 1>(seed, prime);
                } else {
                    return collides<I + 1>(seed, prime);
                }
            } else {
                return false;
            }
        }

        /* Find an FNV-1a seed and prime that produces perfect hashes with respect to
        the keyword table size. */
        static consteval std::pair<size_t, size_t> hash_components() {
            constexpr size_t recursion_limit = fnv1a_hash_seed + 100'000;
            size_t seed = fnv1a_hash_seed;
            size_t prime = fnv1a_hash_prime;
            size_t i = 0;
            while (collides<0>(seed, prime)) {
                if (++seed > recursion_limit) {
                    if (++i == 10) {
                        std::cerr << "error: unable to find a perfect hash seed "
                                  << "after 10^6 iterations.  Consider increasing the "
                                  << "recursion limit or reviewing the keyword "
                                  << "argument names for potential issues.\n";
                        std::exit(1);
                    }
                    seed = fnv1a_hash_seed;
                    prime = fnv1a_fallback_primes[i];
                }
            }
            return {seed, prime};
        }

        /* Populate the keyword table with an appropriate callback for each keyword
        argument in the parameter list. */
        template <size_t I>
        static consteval void populate_keyword_table(
            std::array<Callback, keyword_table_size()>& table,
            size_t seed,
            size_t prime
        ) {
            if constexpr (ArgTraits<at<I>>::kw()) {
                constexpr size_t i = keyword_table_index(
                    hash(ArgTraits<at<I>>::name)
                );
                table[i] = {
                    ArgTraits<at<I>>::name,
                    build_mask<I>(),
                    [](PyTypeObject* type) -> bool {
                        using T = typename ArgTraits<at<I>>::type;
                        if constexpr (has_type<T>) {
                            int rc = PyObject_IsSubclass(
                                reinterpret_cast<PyObject*>(type),
                                ptr(Type<T>())
                            );
                            if (rc < 0) {
                                Exception::from_python();
                            }
                            return rc;
                        } else {
                            throw TypeError(
                                "C++ type has no Python equivalent: " +
                                demangle(typeid(T).name())
                            );
                        }
                    },
                    []() -> PyTypeObject* {
                        using T = typename ArgTraits<at<I>>::type;
                        if constexpr (has_type<T>) {
                            return reinterpret_cast<PyTypeObject*>(ptr(Type<T>()));
                        } else {
                            throw TypeError(
                                "C++ type has no Python equivalent: " +
                                demangle(typeid(T).name())
                            );
                        }
                    }
                };
            }
        }

        /* The keyword table itself.  Each entry holds the expected keyword name for
        validation as well as a callback that can be used to validate its type
        dynamically at runtime. */
        static constexpr std::array<Callback, keyword_table_size()> keyword_table = [] {
            std::array<Callback, keyword_table_size()> table;
            []<size_t... Is>(
                std::index_sequence<Is...>,
                std::array<Callback, keyword_table_size()>& table,
                size_t seed,
                size_t prime
            ) {
                (populate_keyword_table<Is>(table, seed, prime), ...);
            }(
                std::make_index_sequence<n>{},
                table,
                hash_components().first,
                hash_components().second
            );
            return table;
        }();

        template <size_t I>
        static Param _key(size_t& hash) {
            constexpr Callback& callback = positional_table[I];
            PyTypeObject* type = callback.type();
            hash = hash_combine(
                hash,
                fnv1a(
                    ArgTraits<at<I>>::name,
                    hash_components().first,
                    hash_components().second
                ),
                reinterpret_cast<size_t>(type)
            );
            return {ArgTraits<at<I>>::name, type, ArgTraits<at<I>>::kind};
        }

        /* After invoking a function with variadic positional arguments, the argument
        iterators must be fully consumed, otherwise there are additional positional
        arguments that were not consumed. */
        template <std::input_iterator Iter, std::sentinel_for<Iter> End>
        static void assert_args_are_exhausted(Iter& iter, const End& end) {
            if (iter != end) {
                std::string message =
                    "too many arguments in positional parameter pack: ['" +
                    repr(*iter);
                while (++iter != end) {
                    message += "', '" + repr(*iter);
                }
                message += "']";
                throw TypeError(message);
            }
        }

        /* Before invoking a function with variadic keyword arguments, those arguments
        must be scanned to ensure each of them are recognized and do not interfere with
        other keyword arguments given in the source signature. */
        template <typename Outer, typename Inner, typename Map>
        static void assert_kwargs_are_recognized(const Map& kwargs) {
            []<size_t... Is>(std::index_sequence<Is...>, const Map& kwargs) {
                std::vector<std::string> extra;
                for (const auto& [key, value] : kwargs) {
                    if (!callback(key)) {
                        extra.push_back(key);
                    }
                }
                if (!extra.empty()) {
                    auto iter = extra.begin();
                    auto end = extra.end();
                    std::string message =
                        "unexpected keyword arguments: ['" + repr(*iter);
                    while (++iter != end) {
                        message += "', '" + repr(*iter);
                    }
                    message += "']";
                    throw TypeError(message);
                }
            }(std::make_index_sequence<Outer::n>{}, kwargs);
        }

    public:

        /* A seed for an FNV-1a hash algorithm that was found to perfectly hash the
        keyword argument names from the enclosing parameter list. */
        static constexpr size_t seed = hash_components().first;

        /* A prime for an FNV-1a hash algorithm that was found to perfectly hash the
        keyword argument names from the enclosing parameter list. */
        static constexpr size_t prime = hash_components().second;

        /* Hash a byte string according to the FNV-1a algorithm using the seed and
        prime that were found at compile time to perfectly hash the keyword
        arguments. */
        static constexpr size_t hash(const char* str) noexcept {
            return fnv1a(str, seed, prime);
        }
        static constexpr size_t hash(std::string_view str) noexcept {
            return fnv1a(str.data(), seed, prime);
        }
        static constexpr size_t hash(const std::string& str) noexcept {
            return fnv1a(str.data(), seed, prime);
        }

        /* Look up a positional argument, returning a callback object that can be used
        to efficiently validate it.  If the index does not correspond to a recognized
        positional argument, a null callback will be returned that evaluates to false
        under boolean logic.  If the parameter list accepts variadic positional
        arguments, then the variadic argument's callback will be returned instead. */
        static constexpr Callback& callback(size_t i) noexcept {
            if constexpr (has_args) {
                constexpr Callback& args_callback = positional_table[args_idx];
                return i < args_idx ? positional_table[i] : args_callback;
            } else if constexpr (has_kwonly) {
                return i < kwonly_idx ? positional_table[i] : null_callback;
            } else {
                return i < kwargs_idx ? positional_table[i] : null_callback;
            }
        }

        /* Look up a keyword argument, returning a callback object that can be used to
        efficiently validate it.  If the argument name is not recognized, a null
        callback will be returned that evaluates to false under boolean logic.  If the
        parameter list accepts variadic keyword arguments, then the variadic argument's
        callback will be returned instead. */
        static constexpr Callback& callback(std::string_view name) noexcept {
            const Callback& callback = keyword_table[
                keyword_table_index(hash(name.data()))
            ];
            if (callback.name == name) {
                return callback;
            } else {
                if constexpr (has_kwargs) {
                    constexpr Callback& kwargs_callback = keyword_table[kwargs_idx];
                    return kwargs_callback;
                } else {
                    return null_callback;
                }
            }
        }

        /* Produce an overload key that matches the enclosing parameter list. */
        static Params<std::array<Param, n>> key() {
            size_t hash = 0;
            return {
                []<size_t... Is>(std::index_sequence<Is...>, size_t& hash) {
                    return std::array<Param, n>{_key<Is>(hash)...};
                }(std::make_index_sequence<n>{}, hash),
                hash
            };
        }

        /* A tuple holding the default values for each argument in the enclosing
        parameter list marked as optional. */
        struct Defaults {
        private:
            using Outer = Arguments;

            /* The type of a single value in the defaults tuple.  The templated index
            is used to correlate the default value with its corresponding argument in
            the enclosing signature. */
            template <size_t I>
            struct Value  {
                using type = ArgTraits<Outer::at<I>>::type;
                static constexpr StaticStr name = ArgTraits<Outer::at<I>>::name;
                static constexpr size_t index = I;
                std::remove_reference_t<type> value;

                type get(this auto& self) {
                    if constexpr (std::is_rvalue_reference_v<type>) {
                        return std::remove_cvref_t<type>(self.value);
                    } else {
                        return self.value;
                    }
                }
            };

            /* Build a sub-signature holding only the arguments marked as optional from
            the enclosing signature.  This will be a specialization of the enclosing
            class, which is used to bind arguments to this class's constructor using
            the same semantics as the function's call operator. */
            template <typename Sig, typename... Ts>
            struct _Inner { using type = Sig; };
            template <typename... Sig, typename T, typename... Ts>
            struct _Inner<Arguments<Sig...>, T, Ts...> {
                template <typename U>
                struct sub_signature {
                    using type = _Inner<Arguments<Sig...>, Ts...>::type;
                };
                template <typename U> requires (ArgTraits<U>::opt())
                struct sub_signature<U> {
                    using type =_Inner<
                        Arguments<Sig..., typename ArgTraits<U>::no_opt>,
                        Ts...
                    >::type;
                };
                using type = sub_signature<T>::type;
            };
            using Inner = _Inner<Arguments<>, Args...>::type;

            /* Build a std::tuple of Value<I> instances to hold the default values
            themselves. */
            template <size_t I, typename Tuple, typename... Ts>
            struct _Tuple { using type = Tuple; };
            template <size_t I, typename... Part, typename T, typename... Ts>
            struct _Tuple<I, std::tuple<Part...>, T, Ts...> {
                template <typename U>
                struct tuple {
                    using type = _Tuple<I + 1, std::tuple<Part...>, Ts...>::type;
                };
                template <typename U> requires (ArgTraits<U>::opt())
                struct tuple<U> {
                    using type = _Tuple<I + 1, std::tuple<Part..., Value<I>>, Ts...>::type;
                };
                using type = tuple<T>::type;
            };
            using Tuple = _Tuple<0, std::tuple<>, Args...>::type;

            template <size_t I, typename T>
            static constexpr size_t _find = 0;
            template <size_t I, typename T, typename... Ts>
            static constexpr size_t _find<I, std::tuple<T, Ts...>> =
                (I == T::index) ? 0 : 1 + _find<I, std::tuple<Ts...>>;

        public:
            static constexpr size_t n               = Inner::n;
            static constexpr size_t n_posonly       = Inner::n_posonly;
            static constexpr size_t n_pos           = Inner::n_pos;
            static constexpr size_t n_kw            = Inner::n_kw;
            static constexpr size_t n_kwonly        = Inner::n_kwonly;

            template <StaticStr Name>
            static constexpr bool has               = Inner::template has<Name>;
            static constexpr bool has_posonly       = Inner::has_posonly;
            static constexpr bool has_pos           = Inner::has_pos;
            static constexpr bool has_kw            = Inner::has_kw;
            static constexpr bool has_kwonly        = Inner::has_kwonly;

            template <StaticStr Name> requires (has<Name>)
            static constexpr size_t idx             = Inner::template idx<Name>;
            static constexpr size_t kw_idx          = Inner::kw_idx;
            static constexpr size_t kwonly_idx      = Inner::kwonly_idx;

            template <size_t I>
            static constexpr bool is_positional = Inner::template is_positional<I>;
            template <size_t I>
            static constexpr bool is_keyword = Inner::template is_keyword<I>;

            template <size_t I> requires (I < n)
            using at = Inner::template at<I>;
            template <size_t I> requires (is_positional<I>)
            using Arg = Inner::template Arg<I>;
            template <StaticStr Name> requires (has<Name> && is_keyword<idx<Name>>)
            using Kwarg = Inner::template Kwarg<Name>;

            /* Bind an argument list to the default values tuple using the
            sub-signature's normal Bind<> machinery. */
            template <typename... Values>
            using Bind = Inner::template Bind<Values...>;

            /* Given an index into the enclosing signature, find the corresponding index
            in the defaults tuple if that index corresponds to a default value. */
            template <size_t I> requires (ArgTraits<typename Outer::at<I>>::opt())
            static constexpr size_t find = _find<I, Tuple>;

        private:

            template <size_t I, typename... Values>
            static constexpr decltype(auto) unpack(Values&&... values) {
                using observed = Arguments<Values...>;
                using T = Inner::template at<I>;
                constexpr StaticStr name = ArgTraits<T>::name;

                if constexpr (ArgTraits<T>::kwonly()) {
                    constexpr size_t idx = observed::template idx<name>;
                    return impl::unpack_arg<idx>(std::forward<Values>(values)...).value;

                } else if constexpr (ArgTraits<T>::kw()) {
                    if constexpr (I < observed::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else {
                        constexpr size_t idx = observed::template idx<name>;
                        return impl::unpack_arg<idx>(std::forward<Values>(values)...).value;
                    }

                } else {
                    return impl::unpack_arg<I>(std::forward<Values>(values)...);
                }
            }

            template <
                size_t I,
                typename... Values,
                std::input_iterator Iter,
                std::sentinel_for<Iter> End
            >
            static constexpr decltype(auto) unpack(
                size_t args_size,
                Iter& iter,
                const End& end,
                Values&&... values
            ) {
                using observed = Arguments<Values...>;
                using T = Inner::template at<I>;
                constexpr StaticStr name = ArgTraits<T>::name;

                if constexpr (ArgTraits<T>::kwonly()) {
                    constexpr size_t idx = observed::template idx<name>;
                    return impl::unpack_arg<idx>(std::forward<Values>(values)...).value;

                } else if constexpr (ArgTraits<T>::kw()) {
                    if constexpr (I < observed::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else {
                        if (iter != end) {
                            if constexpr (observed::template has<name>) {
                                throw TypeError(
                                    "conflicting values for parameter '" + name +
                                    "' at index " + std::to_string(I)
                                );
                            } else {
                                decltype(auto) result = *iter;
                                ++iter;
                                return result;
                            }

                        } else {
                            if constexpr (observed::template has<name>) {
                                constexpr size_t idx = observed::template idx<name>;
                                return impl::unpack_arg<idx>(
                                    std::forward<Values>(values)...
                                ).value;
                            } else {
                                throw TypeError(
                                    "no match for parameter '" + name +
                                    "' at index " + std::to_string(I)
                                );
                            }
                        }
                    }

                } else {
                    if constexpr (I < observed::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else {
                        if (iter != end) {
                            decltype(auto) result = *iter;
                            ++iter;
                            return result;
                        } else {
                            throw TypeError(
                                "no match for positional-only parmater at index " +
                                std::to_string(I)
                            );
                        }
                    }
                }
            }

            template <
                size_t I,
                typename... Values,
                typename Map
            >
            static constexpr decltype(auto) unpack(
                const Map& map,
                Values&&... values
            ) {
                using observed = Arguments<Values...>;
                using T = Inner::template at<I>;
                constexpr StaticStr name = ArgTraits<T>::name;

                if constexpr (ArgTraits<T>::kwonly()) {
                    auto item = map.find(name);
                    if constexpr (observed::template has<name>) {
                        if (item != map.end()) {
                            throw TypeError(
                                "conflicting values for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        constexpr size_t idx = observed::template idx<name>;
                        return impl::unpack_arg<idx>(
                            std::forward<Values>(values)...
                        ).value;
                    } else {
                        if (item != map.end()) {
                            return item->second;
                        } else {
                            throw TypeError(
                                "no match for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                    }

                } else if constexpr (ArgTraits<T>::kw()) {
                    auto item = map.find(name);
                    if constexpr (I < observed::kw_idx) {
                        if (item != map.end()) {
                            throw TypeError(
                                "conflicting values for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else if constexpr (observed::template has<name>) {
                        if (item != map.end()) {
                            throw TypeError(
                                "conflicting values for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        constexpr size_t idx = observed::template idx<name>;
                        return impl::unpack_arg<idx>(
                            std::forward<Values>(values)...
                        ).value;
                    } else {
                        if (item != map.end()) {
                            return item->second;
                        } else {
                            throw TypeError(
                                "no match for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                    }

                } else {
                    return impl::unpack_arg<I>(std::forward<Values>(values)...);
                }
            }

            template <
                size_t I,
                typename... Values,
                std::input_iterator Iter,
                std::sentinel_for<Iter> End,
                typename Map
            >
            static constexpr decltype(auto) unpack(
                size_t args_size,
                Iter& iter,
                const End& end,
                const Map& map,
                Values&&... values
            ) {
                using observed = Arguments<Values...>;
                using T = Inner::template at<I>;
                constexpr StaticStr name = ArgTraits<T>::name;

                if constexpr (ArgTraits<T>::kwonly()) {
                    return unpack<I>(
                        map,
                        std::forward<Values>(values)...
                    );

                } else if constexpr (ArgTraits<T>::kw()) {
                    auto item = map.find(name);
                    if constexpr (I < observed::kw_idx) {
                        if (item != map.end()) {
                            throw TypeError(
                                "conflicting values for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else {
                        if (iter != end) {
                            if constexpr (observed::template has<name>) {
                                throw TypeError(
                                    "conflicting values for parameter '" + name +
                                    "' at index " + std::to_string(I)
                                );
                            } else {
                                if (item != map.end()) {
                                    throw TypeError(
                                        "conflicting values for parameter '" + name +
                                        "' at index " + std::to_string(I)
                                    );
                                } else {
                                    decltype(auto) result = *iter;
                                    ++iter;
                                    return result;
                                }
                            }
                        } else {
                            if constexpr (observed::template has<name>) {
                                if (item != map.end()) {
                                    throw TypeError(
                                        "conflicting values for parameter '" + name +
                                        "' at index " + std::to_string(I)
                                    );
                                } else {
                                    constexpr size_t idx = observed::template idx<name>;
                                    return impl::unpack_arg<idx>(
                                        std::forward<Values>(values)...
                                    ).value;
                                }
                            } else {
                                if (item != map.end()) {
                                    return item->second;
                                } else {
                                    throw TypeError(
                                        "no match for parameter '" + name +
                                        "' at index " + std::to_string(I)
                                    );
                                }
                            }
                        }
                    }

                } else {
                    return unpack<I>(
                        args_size,
                        iter,
                        end,
                        std::forward<Values>(values)...
                    );
                }
            }

            template <size_t... Is, typename... Values>
            static constexpr Tuple build(
                std::index_sequence<Is...>,
                Values&&... values
            ) {
                using observed = Arguments<Values...>;

                if constexpr (observed::has_args && observed::has_kwargs) {
                    const auto& kwargs = impl::unpack_arg<observed::kwargs_idx>(
                        std::forward<Values>(values)...
                    );
                    assert_kwargs_are_recognized<Inner, observed>(kwargs);
                    const auto& args = impl::unpack_arg<observed::args_idx>(
                        std::forward<Values>(values)...
                    );
                    auto iter = std::ranges::begin(args);
                    auto end = std::ranges::end(args);
                    Tuple result = {{
                        unpack<Is>(
                            std::ranges::size(args),
                            iter,
                            end,
                            kwargs,
                            std::forward<Values>(values)...
                        )
                    }...};
                    assert_args_are_exhausted(iter, end);
                    return result;

                } else if constexpr (observed::has_args) {
                    const auto& args = impl::unpack_arg<observed::args_idx>(
                        std::forward<Values>(values)...
                    );
                    auto iter = std::ranges::begin(args);
                    auto end = std::ranges::end(args);
                    Tuple result = {{
                        unpack<Is>(
                            std::ranges::size(args),
                            iter,
                            end,
                            std::forward<Values>(values)...
                        )
                    }...};
                    assert_args_are_exhausted(iter, end);
                    return result;

                } else if constexpr (observed::has_kwargs) {
                    const auto& kwargs = impl::unpack_arg<observed::kwargs_idx>(
                        std::forward<Values>(values)...
                    );
                    assert_kwargs_are_recognized<Inner, observed>(kwargs);
                    return {{unpack<Is>(kwargs, std::forward<Values>(values)...)}...};

                } else {
                    return {{unpack<Is>(std::forward<Values>(values)...)}...};
                }
            }

        public:
            Tuple values;

            /* The default values' constructor takes Python-style arguments just like
            the call operator, and is only enabled if the call signature is well-formed
            and all optional arguments have been accounted for. */
            template <typename... Values> requires (Bind<Values...>::enable)
            constexpr Defaults(Values&&... values) : values(build(
                std::make_index_sequence<Inner::n>{},
                std::forward<Values>(values)...
            )) {}
            constexpr Defaults(const Defaults& other) = default;
            constexpr Defaults(Defaults&& other) = default;

            /* Get the default value at index I of the tuple.  Use find<> to correlate
            an index from the enclosing signature if needed. */
            template <size_t I> requires (I < n)
            decltype(auto) get() {
                return std::get<I>(values).get();
            }

            /* Get the default value at index I of the tuple.  Use find<> to correlate
            an index from the enclosing signature if needed. */
            template <size_t I> requires (I < n)
            decltype(auto) get() const {
                return std::get<I>(values).get();
            }

            /* Get the default value associated with the named argument, if it is
            marked as optional. */
            template <StaticStr Name> requires (has<Name>)
            decltype(auto) get() {
                return std::get<idx<Name>>(values).get();
            }

            /* Get the default value associated with the named argument, if it is
            marked as optional. */
            template <StaticStr Name> requires (has<Name>)
            decltype(auto) get() const {
                return std::get<idx<Name>>(values).get();
            }

        };

        /// TODO: add a method to Bind<> that produces a simplified Params list from a valid
        /// C++ parameter list.  There also needs to be a cousin for Python calls that
        /// does the same thing from a vectorcall argument array.  Both of the latter
        /// can apply the simplified logic, since it will be used in the call operator.
        /// -> This key() method will also return a std::array-based Params list,
        /// similar to the above.

        /* A helper that binds C++ arguments to the enclosing signature and performs
        the necessary translation to invoke a matching C++ or Python function. */
        template <typename... Values>
        struct Bind {
        private:
            using Outer = Arguments;
            using Inner = Arguments<Values...>;

            template <size_t I>
            using Target = Outer::template at<I>;
            template <size_t J>
            using Source = Inner::template at<J>;

            /* Upon encountering a variadic positional pack in the target signature,
            recursively traverse the remaining source positional arguments and ensure
            that each is convertible to the target type. */
            template <size_t J, typename T>
            static consteval bool consume_target_args() {
                if constexpr (J < Inner::kw_idx && J < Inner::kwargs_idx) {
                    return std::convertible_to<
                        typename ArgTraits<Source<J>>::type,
                        typename ArgTraits<T>::type
                    > && consume_target_args<J + 1, T>();
                } else {
                    return true;
                }
            }

            /* Upon encountering a variadic keyword pack in the target signature,
            recursively traverse the source keyword arguments and extract any that
            aren't present in the target signature, ensuring they are convertible to
            the target type. */
            template <size_t J, typename T>
            static consteval bool consume_target_kwargs() {
                if constexpr (J < Inner::n) {
                    return (
                        Outer::template has<ArgTraits<Source<J>>::name> ||
                        std::convertible_to<
                            typename ArgTraits<Source<J>>::type,
                            typename ArgTraits<T>::type
                        >
                    ) && consume_target_kwargs<J + 1, T>();
                } else {
                    return true;
                }
            }

            /* Upon encountering a variadic positional pack in the source signature,
            recursively traverse the target positional arguments and ensure that the source
            type is convertible to each target type. */
            template <size_t I, typename S>
            static consteval bool consume_source_args() {
                if constexpr (I < kwonly_idx && I < kwargs_idx) {
                    return std::convertible_to<
                        typename ArgTraits<S>::type,
                        typename ArgTraits<Target<I>>::type
                    > && consume_source_args<I + 1, S>();
                } else {
                    return true;
                }
            }

            /* Upon encountering a variadic keyword pack in the source signature,
            recursively traverse the target keyword arguments and extract any that aren't
            present in the source signature, ensuring that the source type is convertible
            to each target type. */
            template <size_t I, typename S>
            static consteval bool consume_source_kwargs() {
                if constexpr (I < Outer::n) {
                    return (
                        Inner::template has<ArgTraits<Target<I>>::name> ||
                        std::convertible_to<
                            typename ArgTraits<S>::type,
                            typename ArgTraits<Target<I>>::type
                        >
                    ) && consume_source_kwargs<I + 1, S>();
                } else {
                    return true;
                }
            }

            /* Recursively check whether the source arguments conform to Python calling
            conventions (i.e. no positional arguments after a keyword, no duplicate
            keywords, etc.), fully satisfy the target signature, and are convertible to
            the expected types, after accounting for parameter packs in both signatures.

            The actual algorithm is quite complex, especially since it is evaluated at
            compile time via template recursion and must validate both signatures at
            once.  Here's a rough outline of how it works:

                1.  Generate two indices, I and J, which are used to traverse over the
                    target and source signatures, respectively.
                2.  For each I, J, inspect the target argument at index I:
                    a.  If the target argument is positional-only, check that the
                        source argument is not a keyword, otherwise check that the
                        target argument is marked as optional.
                    b.  If the target argument is positional-or-keyword, check that the
                        source argument meets the criteria for (a), or that the source
                        signature contains a matching keyword argument.
                    c.  If the target argument is keyword-only, check that the source's
                        positional arguments have been exhausted, and that the source
                        signature contains a matching keyword argument or the target
                        argument is marked as optional.
                    d.  If the target argument is variadic positional, recur until all
                        source positional arguments have been exhausted, ensuring that
                        each is convertible to the target type.
                    e.  If the target argument is variadic keyword, recur until all
                        source keyword arguments have been exhausted, ensuring that
                        each is convertible to the target type.
                3.  Then inspect the source argument at index J:
                    a.  If the source argument is positional, check that it is
                        convertible to the target type.
                    b.  If the source argument is keyword, check that the target
                        signature contains a matching keyword argument, and that the
                        source argument is convertible to the target type.
                    c.  If the source argument is variadic positional, recur until all
                        target positional arguments have been exhausted, ensuring that
                        each is convertible from the source type.
                    d.  If the source argument is variadic keyword, recur until all
                        target keyword arguments have been exhausted, ensuring that
                        each is convertible from the source type.
                4.  Advance to the next argument pair and repeat until all arguments
                    have been checked.  If there are more target arguments than source
                    arguments, then the remaining target arguments must be optional or
                    variadic.  If there are more source arguments than target
                    arguments, then we return false, as the only way to satisfy the
                    target signature is for it to include variadic arguments, which
                    would have avoided this case.

            If all of these conditions are met, then the call operator is enabled and
            the arguments can be translated by the reordering operators listed below.
            Otherwise, the call operator is disabled and the arguments are rejected in
            a way that allows LSPs to provide informative feedback to the user. */
            template <size_t I, size_t J>
            static consteval bool enable_recursive() {
                // both lists are satisfied
                if constexpr (I >= Outer::n && J >= Inner::n) {
                    return true;

                // there are extra source arguments
                } else if constexpr (I >= Outer::n) {
                    return false;

                // there are extra target arguments
                } else if constexpr (J >= Inner::n) {
                    using T = Target<I>;
                    if constexpr (ArgTraits<T>::opt() || ArgTraits<T>::args()) {
                        return enable_recursive<I + 1, J>();
                    } else if constexpr (ArgTraits<T>::kwargs()) {
                        return consume_target_kwargs<Inner::kw_idx, T>();
                    }
                    return false;  // a required argument is missing

                // both lists have arguments remaining
                } else {
                    using T = Target<I>;
                    using S = Source<J>;

                    // ensure target arguments are present without conflict + expand
                    // parameter packs over source arguments
                    if constexpr (ArgTraits<T>::posonly()) {
                        if constexpr (
                            (
                                ArgTraits<T>::name != "" &&
                                Inner::template has<ArgTraits<T>::name>
                            ) || (
                                !ArgTraits<T>::opt() &&
                                Inner::template is_keyword<J>
                            )
                        ) {
                            return false;
                        }
                    } else if constexpr (ArgTraits<T>::pos()) {
                        if constexpr (
                            (
                                Inner::template is_positional<J> &&
                                Inner::template has<ArgTraits<T>::name>
                            ) || (
                                Inner::template is_keyword<J> &&
                                !ArgTraits<T>::opt() &&
                                !Inner::template has<ArgTraits<T>::name>
                            )
                        ) {
                            return false;
                        }
                    } else if constexpr (ArgTraits<T>::kwonly()) {
                        if constexpr (
                            Inner::template is_positional<J> || (
                                !ArgTraits<T>::opt() &&
                                !Inner::template has<ArgTraits<T>::name>
                            )
                        ) {
                            return false;
                        }
                    } else if constexpr (ArgTraits<T>::args()) {
                        return consume_target_args<J, T>() && enable_recursive<
                            I + 1,
                            Inner::has_kw ? Inner::kw_idx : Inner::kwargs_idx
                        >();  // skip ahead to source keywords
                    } else if constexpr (ArgTraits<T>::kwargs()) {
                        return consume_target_kwargs<
                            Inner::has_kw ? Inner::kw_idx : Inner::kwargs_idx,
                            T
                        >();  // end of expression
                    } else {
                        return false;  // not reachable
                    }

                    // ensure source arguments match targets & expand parameter packs
                    if constexpr (ArgTraits<S>::posonly()) {
                        if constexpr (!std::convertible_to<
                            typename ArgTraits<S>::type,
                            typename ArgTraits<T>::type
                        >) {
                            return false;
                        }
                    } else if constexpr (ArgTraits<S>::kw()) {
                        if constexpr (Outer::template has<ArgTraits<S>::name>) {
                            using T2 = Target<Outer::template idx<ArgTraits<S>::name>>;
                            if constexpr (!std::convertible_to<
                                typename ArgTraits<S>::type,
                                typename ArgTraits<T2>::type
                            >) {
                                return false;
                            }
                        } else if constexpr (Outer::has_kwargs) {
                            if constexpr (!std::convertible_to<
                                typename ArgTraits<S>::type,
                                typename ArgTraits<Target<Outer::kwargs_idx>>::type
                            >) {
                                return false;
                            }
                        } else {
                            return false;
                        }
                    } else if constexpr (ArgTraits<S>::args()) {
                        return consume_source_args<I, S>() && enable_recursive<
                            Outer::has_kwonly ? Outer::kwonly_idx : Outer::kwargs_idx,
                            J + 1
                        >();  // skip to target keywords
                    } else if constexpr (ArgTraits<S>::kwargs()) {
                        return consume_source_kwargs<I, S>();  // end of expression
                    } else {
                        return false;  // not reachable
                    }

                    // advance to next argument pair
                    return enable_recursive<I + 1, J + 1>();
                }
            }

            /// TODO: no idea if build_kwargs() is correct or necessary

            template <size_t I, typename T>
            static constexpr void build_kwargs(
                std::unordered_map<std::string, T>& map,
                Values&&... args
            ) {
                using Arg = Source<Inner::kw_idx + I>;
                if constexpr (!Outer::template has<ArgTraits<Arg>::name>) {
                    map.emplace(
                        ArgTraits<Arg>::name,
                        impl::unpack_arg<Inner::kw_index + I>(
                            std::forward<Source>(args)...
                        )
                    );
                }
            }

            /* The cpp_to_cpp() method is used to convert an index sequence over the
             * enclosing signature into the corresponding values pulled from either the
             * call site or the function's defaults.  It is complicated by the presence
             * of variadic parameter packs in both the target signature and the call
             * arguments, which have to be handled as a cross product of possible
             * combinations.
             *
             * Unless a variadic parameter pack is given at the call site, all of these
             * are resolved entirely at compile time by reordering the arguments using
             * template recursion.  However, because the size of a variadic parameter
             * pack cannot be determined at compile time, calls that use these will have
             * to extract values at runtime, and may therefore raise an error if a
             * corresponding value does not exist in the parameter pack, or if there are
             * extras that are not included in the target signature.
             */

            /// TODO: this side of things might need modifications to be able to handle
            /// the `self` parameter.

            template <size_t I>
            static constexpr ArgTraits<Target<I>>::type cpp_to_cpp(
                const Defaults& defaults,
                Values&&... values
            ) {
                using T = Target<I>;
                using type = ArgTraits<T>::type;
                constexpr StaticStr name = ArgTraits<T>::name;

                if constexpr (ArgTraits<T>::kwonly()) {
                    if constexpr (Inner::template has<name>) {
                        constexpr size_t idx = Inner::template idx<name>;
                        return impl::unpack_arg<idx>(std::forward<Values>(values)...);
                    } else {
                        return defaults.template get<I>();
                    }

                } else if constexpr (ArgTraits<T>::kw()) {
                    if constexpr (I < Inner::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else if constexpr (Inner::template has<name>) {
                        constexpr size_t idx = Inner::template idx<name>;
                        return impl::unpack_arg<idx>(std::forward<Values>(values)...);
                    } else {
                        return defaults.template get<I>();
                    }

                } else if constexpr (ArgTraits<T>::args()) {
                    using Pack = std::vector<type>;
                    Pack vec;
                    if constexpr (I < Inner::kw_idx) {
                        constexpr size_t diff = Inner::kw_idx - I;
                        vec.reserve(diff);
                        []<size_t... Js>(
                            std::index_sequence<Js...>,
                            Pack& vec,
                            Values&&... args
                        ) {
                            (vec.push_back(
                                impl::unpack_arg<I + Js>(std::forward<Values>(args)...)
                            ), ...);
                        }(
                            std::make_index_sequence<diff>{},
                            vec,
                            std::forward<Values>(values)...
                        );
                    }
                    return vec;

                } else if constexpr (ArgTraits<T>::kwargs()) {
                    using Pack = std::unordered_map<std::string, type>;
                    Pack pack;
                    []<size_t... Js>(
                        std::index_sequence<Js...>,
                        Pack& pack,
                        Values&&... args
                    ) {
                        (build_kwargs<Js>(pack, std::forward<Values>(args)...), ...);
                    }(
                        std::make_index_sequence<Inner::n - Inner::kw_idx>{},
                        pack,
                        std::forward<Values>(values)...
                    );
                    return pack;

                } else {
                    if constexpr (I < Inner::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else {
                        return defaults.template get<I>();
                    }
                }
            }

            template <size_t I, std::input_iterator Iter, std::sentinel_for<Iter> End>
            static constexpr ArgTraits<Target<I>>::type cpp_to_cpp(
                const Defaults& defaults,
                size_t args_size,
                Iter& iter,
                const End& end,
                Values&&... values
            ) {
                using T = Target<I>;
                using type = ArgTraits<T>::type;
                constexpr StaticStr name = ArgTraits<T>::name;

                if constexpr (ArgTraits<T>::kwonly()) {
                    return cpp_to_cpp<I>(defaults, std::forward<Values>(values)...);

                } else if constexpr (ArgTraits<T>::kw()) {
                    if constexpr (I < Inner::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else {
                        if (iter != end) {
                            if constexpr (Inner::template has<name>) {
                                throw TypeError(
                                    "conflicting values for parameter '" + name +
                                    "' at index " + std::to_string(I)
                                );
                            } else {
                                decltype(auto) result = *iter;
                                ++iter;
                                return result;
                            }

                        } else {
                            if constexpr (Inner::template has<name>) {
                                constexpr size_t idx = Inner::template idx<name>;
                                return impl::unpack_arg<idx>(std::forward<Values>(values)...);
                            } else {
                                if constexpr (ArgTraits<T>::opt()) {
                                    return defaults.template get<I>();
                                } else {
                                    throw TypeError(
                                        "no match for parameter '" + name +
                                        "' at index " + std::to_string(I)
                                    );
                                }
                            }
                        }
                    }

                } else if constexpr (ArgTraits<T>::args()) {
                    using Pack = std::vector<type>;  /// TODO: can't store references
                    Pack vec;
                    if constexpr (I < Inner::args_idx) {
                        constexpr size_t diff = Inner::args_idx - I;
                        vec.reserve(diff + args_size);
                        []<size_t... Js>(
                            std::index_sequence<Js...>,
                            Pack& vec,
                            Values&&... args
                        ) {
                            (vec.push_back(
                                impl::unpack_arg<I + Js>(std::forward<Values>(args)...)
                            ), ...);
                        }(
                            std::make_index_sequence<diff>{},
                            vec,
                            std::forward<Values>(values)...
                        );
                        vec.insert(vec.end(), iter, end);
                    }
                    return vec;

                } else if constexpr (ArgTraits<T>::kwargs()) {
                    return cpp_to_cpp<I>(defaults, std::forward<Values>(values)...);

                } else {
                    if constexpr (I < Inner::kw_idx) {
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else {
                        if (iter != end) {
                            decltype(auto) result = *iter;
                            ++iter;
                            return result;
                        } else {
                            if constexpr (ArgTraits<T>::opt()) {
                                return defaults.template get<I>();
                            } else {
                                throw TypeError(
                                    "no match for positional-only parameter at index " +
                                    std::to_string(I)
                                );
                            }
                        }
                    }
                }
            }

            template <size_t I, typename Mapping>
            static constexpr ArgTraits<Target<I>>::type cpp_to_cpp(
                const Defaults& defaults,
                const Mapping& map,
                Values&&... values
            ) {
                using T = Target<I>;
                using type = ArgTraits<T>::type;
                constexpr StaticStr name = ArgTraits<T>::name;

                if constexpr (ArgTraits<T>::kwonly()) {
                    auto item = map.find(name);
                    if constexpr (Inner::template has<name>) {
                        if (item != map.end()) {
                            throw TypeError(
                                "conflicting value for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        constexpr size_t idx = Inner::template idx<name>;
                        return impl::unpack_arg<idx>(std::forward<Values>(values)...);
                    } else {
                        if (item != map.end()) {
                            return item->second;
                        } else {
                            if constexpr (ArgTraits<T>::opt()) {
                                return defaults.template get<I>();
                            } else {
                                throw TypeError(
                                    "no match for parameter '" + name +
                                    "' at index " + std::to_string(I)
                                );
                            }
                        }
                    }

                } else if constexpr (ArgTraits<T>::kw()) {
                    auto item = map.find(name);
                    if constexpr (I < Inner::kw_idx) {
                        if (item != map.end()) {
                            throw TypeError(
                                "conflicting value for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else if constexpr (Inner::template has<name>) {
                        if (item != map.end()) {
                            throw TypeError(
                                "conflicting value for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        constexpr size_t idx = Inner::template idx<name>;
                        return impl::unpack_arg<idx>(std::forward<Values>(values)...);
                    } else {
                        if (item != map.end()) {
                            return item->second;
                        } else {
                            if constexpr (ArgTraits<T>::opt()) {
                                return defaults.template get<I>();
                            } else {
                                throw TypeError(
                                    "no match for parameter '" + name +
                                    "' at index " + std::to_string(I)
                                );
                            }
                        }
                    }

                } else if constexpr (ArgTraits<T>::args()) {
                    return cpp_to_cpp<I>(defaults, std::forward<Values>(values)...);

                } else if constexpr (ArgTraits<T>::kwargs()) {
                    using Pack = std::unordered_map<std::string, type>;
                    Pack pack;
                    []<size_t... Js>(
                        std::index_sequence<Js...>,
                        Pack& pack,
                        Values&&... values
                    ) {
                        (build_kwargs<Js>(pack, std::forward<Values>(values)...), ...);
                    }(
                        std::make_index_sequence<Inner::n - Inner::kw_idx>{},
                        pack,
                        std::forward<Values>(values)...
                    );
                    for (const auto& [key, value] : map) {
                        if (pack.contains(key)) {
                            throw TypeError(
                                "duplicate value for parameter '" + key + "'"
                            );
                        }
                        pack[key] = value;
                    }
                    return pack;

                } else {
                    return cpp_to_cpp<I>(defaults, std::forward<Values>(values)...);
                }
            }

            template <
                size_t I,
                std::input_iterator Iter,
                std::sentinel_for<Iter> End,
                typename Mapping
            >
            static constexpr ArgTraits<Target<I>>::type cpp_to_cpp(
                const Defaults& defaults,
                size_t args_size,
                Iter& iter,
                const End& end,
                const Mapping& map,
                Values&&... values
            ) {
                using T = Target<I>;
                using type = ArgTraits<T>::type;
                constexpr StaticStr name = ArgTraits<T>::name;

                if constexpr (ArgTraits<T>::kwonly()) {
                    return cpp_to_cpp<I>(
                        defaults,
                        map,
                        std::forward<Values>(values)...
                    );

                } else if constexpr (ArgTraits<T>::kw()) {
                    auto item = map.find(name);
                    if constexpr (I < Inner::kw_idx) {
                        if (item != map.end()) {
                            throw TypeError(
                                "conflicting values for parameter '" + name +
                                "' at index " + std::to_string(I)
                            );
                        }
                        return impl::unpack_arg<I>(std::forward<Values>(values)...);
                    } else {
                        if (iter != end) {
                            if constexpr (Inner::template has<name>) {
                                throw TypeError(
                                    "conflicting values for parameter '" + name +
                                    "' at index " + std::to_string(I)
                                );
                            } else {
                                if (item != map.end()) {
                                    throw TypeError(
                                        "conflicting values for parameter '" + name +
                                        "' at index " + std::to_string(I)
                                    );
                                } else {
                                    decltype(auto) result = *iter;
                                    ++iter;
                                    return result;
                                }
                            }
                        } else {
                            if constexpr (Inner::template has<name>) {
                                if (item != map.end()) {
                                    throw TypeError(
                                        "conflicting values for parameter '" + name +
                                        "' at index " + std::to_string(I)
                                    );
                                } else {
                                    constexpr size_t idx = Inner::template idx<name>;
                                    return impl::unpack_arg<idx>(
                                        std::forward<Values>(values)...
                                    ).value;
                                }
                            } else {
                                if (item != map.end()) {
                                    return item->second;
                                } else {
                                    if constexpr (ArgTraits<T>::opt()) {
                                        return defaults.template get<I>();
                                    } else {
                                        throw TypeError(
                                            "no match for parameter '" + name +
                                            "' at index " + std::to_string(I)
                                        );
                                    }
                                }
                            }
                        }
                    }

                } else if constexpr (ArgTraits<T>::args()) {
                    return cpp_to_cpp<I>(
                        defaults,
                        args_size,
                        iter,
                        end,
                        std::forward<Values>(values)...
                    );

                } else if constexpr (ArgTraits<T>::kwargs()) {
                    return cpp_to_cpp<I>(
                        defaults,
                        map,
                        std::forward<Values>(values)...
                    );

                } else {
                    return cpp_to_cpp<I>(
                        defaults,
                        args_size,
                        iter,
                        end,
                        std::forward<Values>(values)...
                    );
                }
            }

            /* The py_to_cpp() method is used to convert an index sequence over the
             * enclosing signature into the corresponding values pulled from either an
             * array of Python vectorcall arguments or the function's defaults.  This
             * requires us to parse an array of PyObject* pointers with a binary layout
             * that looks something like this:
             *
             *                          ( kwnames tuple )
             *      -------------------------------------
             *      | x | p | p | p |...| k | k | k |...|
             *      -------------------------------------
             *            ^             ^
             *            |             nargs ends here
             *            *args starts here
             *
             * Where 'x' is an optional first element that can be temporarily written to
             * in order to efficiently forward the `self` argument for bound methods,
             * etc.  The presence of this argument is determined by the
             * PY_VECTORCALL_ARGUMENTS_OFFSET flag, which is encoded in nargs.  You can
             * check for its presence by bitwise AND-ing against nargs, and the true
             * number of arguments must be extracted using `PyVectorcall_NARGS(nargs)`
             * to account for this.
             *
             * If PY_VECTORCALL_ARGUMENTS_OFFSET is set and 'x' is written to, then it must
             * always be reset to its original value before the function returns.  This
             * allows for nested forwarding/scoping using the same argument list, with no
             * extra allocations.
             */

            template <size_t I>
            static ArgTraits<Target<I>>::type py_to_cpp(
                const Defaults& defaults,
                PyObject* const* args,
                size_t nargsf,
                PyObject* kwnames,
                size_t kwcount
            ) {
                using T = Target<I>;
                using type = ArgTraits<T>::type;
                constexpr StaticStr name = ArgTraits<T>::name;
                bool has_self = nargsf & PY_VECTORCALL_ARGUMENTS_OFFSET;
                size_t nargs = PyVectorcall_NARGS(nargsf);

                if constexpr (ArgTraits<T>::kwonly()) {
                    if (kwnames != nullptr) {
                        for (size_t i = 0; i < kwcount; ++i) {
                            const char* kwname = PyUnicode_AsUTF8(
                                PyTuple_GET_ITEM(kwnames, i)
                            );
                            if (kwname == nullptr) {
                                Exception::from_python();
                            } else if (std::strcmp(kwname, name) == 0) {
                                return reinterpret_borrow<Object>(args[i]);
                            }
                        }
                    }
                    if constexpr (ArgTraits<T>::opt()) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "missing required keyword-only argument '" + name + "'"
                        );
                    }

                } else if constexpr (ArgTraits<T>::kw()) {
                    if (I < nargs) {
                        return reinterpret_borrow<Object>(args[I]);
                    } else if (kwnames != nullptr) {
                        for (size_t i = 0; i < kwcount; ++i) {
                            const char* kwname = PyUnicode_AsUTF8(
                                PyTuple_GET_ITEM(kwnames, i)
                            );
                            if (kwname == nullptr) {
                                Exception::from_python();
                            } else if (std::strcmp(kwname, name) == 0) {
                                return reinterpret_borrow<Object>(args[nargs + i]);
                            }
                        }
                    }
                    if constexpr (ArgTraits<T>::opt()) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "missing required argument '" + name + "' at index " +
                            std::to_string(I)
                        );
                    }

                } else if constexpr (ArgTraits<T>::args()) {
                    std::vector<type> vec;
                    for (size_t i = I; i < nargs; ++i) {
                        vec.push_back(reinterpret_borrow<Object>(args[i]));
                    }
                    return vec;

                } else if constexpr (ArgTraits<T>::kwargs()) {
                    std::unordered_map<std::string, type> map;
                    if (kwnames != nullptr) {
                        auto sequence = std::make_index_sequence<Outer::n_kw>{};
                        for (size_t i = 0; i < kwcount; ++i) {
                            Py_ssize_t length;
                            const char* kwname = PyUnicode_AsUTF8AndSize(
                                PyTuple_GET_ITEM(kwnames, i),
                                &length
                            );
                            if (kwname == nullptr) {
                                Exception::from_python();
                            } else if (!Outer::callback(kwname)) {
                                map.emplace(
                                    std::string(kwname, length),
                                    reinterpret_borrow<Object>(args[nargs + i])
                                );
                            }
                        }
                    }
                    return map;

                } else {
                    if (I < nargs) {
                        return reinterpret_borrow<Object>(args[I]);
                    }
                    if constexpr (ArgTraits<T>::opt()) {
                        return defaults.template get<I>();
                    } else {
                        throw TypeError(
                            "missing required positional-only argument at index " +
                            std::to_string(I)
                        );
                    }
                }
            }

            /* The cpp_to_py() method is used to allocate a Python vectorcall argument
             * array and populate it according to a C++ argument list.  This is
             * essentially the inverse of the py_to_cpp() method, and requires us to
             * allocate an array with the same binary layout as described above.  That
             * array can then be used to efficiently call a Python function using the
             * vectorcall protocol, which is the fastest possible way to call a Python
             * function from C++.
             */

            template <size_t J>
            static void cpp_to_py(
                PyObject* kwnames,
                PyObject** args,
                Values&&... values
            ) {
                using S = Source<J>;
                using type = ArgTraits<S>::type;
                constexpr StaticStr name = ArgTraits<S>::name;

                if constexpr (ArgTraits<S>::kw()) {
                    try {
                        PyTuple_SET_ITEM(
                            kwnames,
                            J - Inner::kw_idx,
                            Py_NewRef(TemplateString<name>::ptr)
                        );
                        args[J + 1] = release(as_object(
                            impl::unpack_arg<J>(std::forward<Values>(values)...)
                        ));
                    } catch (...) {
                        for (size_t i = 1; i <= J; ++i) {
                            Py_XDECREF(args[i]);
                        }
                    }

                } else if constexpr (ArgTraits<S>::args()) {
                    size_t curr = J + 1;
                    try {
                        const auto& var_args = impl::unpack_arg<J>(
                            std::forward<Values>(values)...
                        );
                        for (const auto& value : var_args) {
                            args[curr] = release(as_object(value));
                            ++curr;
                        }
                    } catch (...) {
                        for (size_t i = 1; i < curr; ++i) {
                            Py_XDECREF(args[i]);
                        }
                    }

                } else if constexpr (ArgTraits<S>::kwargs()) {
                    size_t curr = J + 1;
                    try {
                        const auto& var_kwargs = impl::unpack_arg<J>(
                            std::forward<Values>(values)...
                        );
                        for (const auto& [key, value] : var_kwargs) {
                            PyObject* name = PyUnicode_FromStringAndSize(
                                key.data(),
                                key.size()
                            );
                            if (name == nullptr) {
                                Exception::from_python();
                            }
                            PyTuple_SET_ITEM(kwnames, curr - Inner::kw_idx, name);
                            args[curr] = release(as_object(value));
                            ++curr;
                        }
                    } catch (...) {
                        for (size_t i = 1; i < curr; ++i) {
                            Py_XDECREF(args[i]);
                        }
                    }

                } else {
                    try {
                        args[J + 1] = release(as_object(
                            impl::unpack_arg<J>(std::forward<Values>(values)...)
                        ));
                    } catch (...) {
                        for (size_t i = 1; i <= J; ++i) {
                            Py_XDECREF(args[i]);
                        }
                    }
                }
            }

        public:
            static constexpr size_t n               = sizeof...(Values);
            static constexpr size_t n_pos           = Inner::n_pos;
            static constexpr size_t n_kw            = Inner::n_kw;

            template <StaticStr Name>
            static constexpr bool has               = Inner::template has<Name>;
            static constexpr bool has_pos           = Inner::has_pos;
            static constexpr bool has_args          = Inner::has_args;
            static constexpr bool has_kw            = Inner::has_kw;
            static constexpr bool has_kwargs        = Inner::has_kwargs;

            template <StaticStr Name> requires (has<Name>)
            static constexpr size_t idx             = Inner::template idx<Name>;
            static constexpr size_t args_idx        = Inner::args_idx;
            static constexpr size_t kw_idx          = Inner::kw_idx;
            static constexpr size_t kwargs_idx      = Inner::kwargs_idx;

            template <size_t I>
            static constexpr bool is_positional = Inner::template is_positional<I>;
            template <size_t I>
            static constexpr bool is_keyword = Inner::template is_keyword<I>;

            template <size_t I> requires (I < n)
            using at = Inner::template at<I>;
            template <size_t I> requires (is_positional<I>)
            using Arg = Inner::template Arg<I>;
            template <StaticStr Name> requires (has<Name> && is_keyword<idx<Name>>)
            using Kwarg = Inner::template Kwarg<Name>;

            /* Call operator is only enabled if source arguments are well-formed and
            match the target signature. */
            static constexpr bool enable =
                Inner::proper_argument_order &&
                Inner::no_duplicate_arguments &&
                enable_recursive<0, 0>();

            /// TODO: both of these call operators need to be updated to handle the
            /// `self` parameter.

            /* Invoke an external C++ function using the given arguments and default
            values. */
            template <typename Func>
                requires (enable && std::is_invocable_v<Func, Args...>)
            static auto operator()(
                const Defaults& defaults,
                Func&& func,
                Values&&... values
            ) -> std::invoke_result_t<Func, Args...> {
                using Return = std::invoke_result_t<Func, Args...>;
                return []<size_t... Is>(
                    std::index_sequence<Is...>,
                    const Defaults& defaults,
                    Func&& func,
                    Values&&... values
                ) {
                    if constexpr (Inner::has_args && Inner::has_kwargs) {
                        const auto& kwargs = impl::unpack_arg<Inner::kwargs_idx>(
                            std::forward<Values>(values)...
                        );
                        if constexpr (!Outer::has_kwargs) {
                            assert_kwargs_are_recognized<Outer, Inner>(kwargs);
                        }
                        const auto& args = impl::unpack_arg<Inner::args_idx>(
                            std::forward<Values>(values)...
                        );
                        auto iter = std::ranges::begin(args);
                        auto end = std::ranges::end(args);
                        if constexpr (std::is_void_v<Return>) {
                            func({
                                cpp_to_cpp<Is>(
                                    defaults,
                                    std::ranges::size(args),
                                    iter,
                                    end,
                                    kwargs,
                                    std::forward<Values>(values)...
                                )
                            }...);
                            if constexpr (!Outer::has_args) {
                                assert_args_are_exhausted(iter, end);
                            }
                        } else {
                            decltype(auto) result = func({
                                cpp_to_cpp<Is>(
                                    defaults,
                                    std::ranges::size(args),
                                    iter,
                                    end,
                                    kwargs,
                                    std::forward<Values>(values)...
                                )
                            }...);
                            if constexpr (!Outer::has_args) {
                                assert_args_are_exhausted(iter, end);
                            }
                            return result;
                        }

                    // variadic positional arguments are passed as an iterator range, which
                    // must be exhausted after the function call completes
                    } else if constexpr (Inner::has_args) {
                        const auto& args = impl::unpack_arg<Inner::args_idx>(
                            std::forward<Values>(values)...
                        );
                        auto iter = std::ranges::begin(args);
                        auto end = std::ranges::end(args);
                        if constexpr (std::is_void_v<Return>) {
                            func({
                                cpp_to_cpp<Is>(
                                    defaults,
                                    std::ranges::size(args),
                                    iter,
                                    end,
                                    std::forward<Values>(values)...
                                )
                            }...);
                            if constexpr (!Outer::has_args) {
                                assert_args_are_exhausted(iter, end);
                            }
                        } else {
                            decltype(auto) result = func({
                                cpp_to_cpp<Is>(
                                    defaults,
                                    std::ranges::size(args),
                                    iter,
                                    end,
                                    std::forward<Source>(values)...
                                )
                            }...);
                            if constexpr (!Outer::has_args) {
                                assert_args_are_exhausted(iter, end);
                            }
                            return result;
                        }

                    // variadic keyword arguments are passed as a dictionary, which must be
                    // validated up front to ensure all keys are recognized
                    } else if constexpr (Inner::has_kwargs) {
                        const auto& kwargs = impl::unpack_arg<Inner::kwargs_idx>(
                            std::forward<Values>(values)...
                        );
                        if constexpr (!Outer::has_kwargs) {
                            assert_kwargs_are_recognized<Outer, Inner>(kwargs);
                        }
                        if constexpr (std::is_void_v<Return>) {
                            func({
                                cpp_to_cpp<Is>(
                                    defaults,
                                    kwargs,
                                    std::forward<Values>(values)...
                                )
                            }...);
                        } else {
                            return func({
                                cpp_to_cpp<Is>(
                                    defaults,
                                    kwargs,
                                    std::forward<Values>(values)...
                                )
                            }...);
                        }

                    // interpose the two if there are both positional and keyword argument packs
                    } else {
                        if constexpr (std::is_void_v<Return>) {
                            func({
                                cpp_to_cpp<Is>(
                                    defaults,
                                    std::forward<Source>(values)...
                                )
                            }...);
                        } else {
                            return func({
                                cpp_to_cpp<Is>(
                                    defaults,
                                    std::forward<Source>(values)...
                                )
                            }...);
                        }
                    }
                }(
                    std::make_index_sequence<Outer::n>{},
                    defaults,
                    std::forward<Func>(func),
                    std::forward<Source>(values)...
                );
            }

            /// TODO: the inclusion of the `self` parameter changes the logic around
            /// calling with no args, one arg, etc.  Perhaps the best thing to do is
            /// to just always use the vectorcall protocol.

            /* Invoke an external Python function using the given arguments.  This will
            always return a new reference to a raw Python object, or throw a runtime
            error if the arguments are malformed in some way. */
            template <typename = void> requires (enable)
            static PyObject* operator()(
                PyObject* func,
                Values&&... values
            ) {
                return []<size_t... Is>(
                    std::index_sequence<Is...>,
                    PyObject* func,
                    Values&&... values
                ) {
                    PyObject* result;

                    // if there are no arguments, we can use the no-args protocol
                    if constexpr (Inner::n == 0) {
                        result = PyObject_CallNoArgs(func);

                    // if there are no variadic arguments, we can stack allocate the argument
                    // array with a fixed size
                    } else if constexpr (!Inner::has_args && !Inner::has_kwargs) {
                        // if there is only one argument, we can use the one-arg protocol
                        if constexpr (Inner::n == 1) {
                            if constexpr (Inner::has_kw) {
                                result = PyObject_CallOneArg(
                                    func,
                                    ptr(as_object(
                                        impl::unpack_arg<0>(
                                            std::forward<Values>(values)...
                                        ).value
                                    ))
                                );
                            } else {
                                result = PyObject_CallOneArg(
                                    func,
                                    ptr(as_object(
                                        impl::unpack_arg<0>(
                                            std::forward<Values>(values)...
                                        )
                                    ))
                                );
                            }

                        // if there is more than one argument, we construct a vectorcall
                        // argument array
                        } else {
                            PyObject* array[Inner::n + 1];
                            array[0] = nullptr;
                            PyObject* kwnames;
                            if constexpr (Inner::has_kw) {
                                kwnames = PyTuple_New(Inner::n_kw);
                            } else {
                                kwnames = nullptr;
                            }
                            (
                                cpp_to_python<Is>(
                                    kwnames,
                                    array,  // 1-indexed
                                    std::forward<Values>(values)...
                                ),
                                ...
                            );
                            Py_ssize_t npos = Inner::n - Inner::n_kw;
                            result = PyObject_Vectorcall(
                                func,
                                array,
                                npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                                kwnames
                            );
                            for (size_t i = 1; i <= Inner::n; ++i) {
                                Py_XDECREF(array[i]);  // release all argument references
                            }
                        }

                    // otherwise, we have to heap-allocate the array with a variable size
                    } else if constexpr (Inner::has_args && !Inner::has_kwargs) {
                        const auto& args = impl::unpack_arg<Inner::args_idx>(
                            std::forward<Values>(values)...
                        );
                        size_t nargs = Inner::n - 1 + std::ranges::size(args);
                        PyObject** array = new PyObject*[nargs + 1];
                        array[0] = nullptr;
                        PyObject* kwnames;
                        if constexpr (Inner::has_kw) {
                            kwnames = PyTuple_New(Inner::n_kw);
                        } else {
                            kwnames = nullptr;
                        }
                        (
                            cpp_to_python<Is>(
                                kwnames,
                                array,
                                std::forward<Values>(values)...
                            ),
                            ...
                        );
                        Py_ssize_t npos = Inner::n - 1 - Inner::n_kw + std::ranges::size(args);
                        result = PyObject_Vectorcall(
                            func,
                            array,
                            npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            nullptr
                        );
                        for (size_t i = 1; i <= Inner::n; ++i) {
                            Py_XDECREF(array[i]);
                        }
                        delete[] array;

                    // The following specializations handle the cross product of
                    // positional/keyword parameter packs which differ only in initialization
                    } else if constexpr (!Inner::has_args && Inner::has_kwargs) {
                        const auto& kwargs = impl::unpack_arg<Inner::kwargs_idx>(
                            std::forward<Values>(values)...
                        );
                        size_t nargs = Inner::n - 1 + std::ranges::size(kwargs);
                        PyObject** array = new PyObject*[nargs + 1];
                        array[0] = nullptr;
                        PyObject* kwnames = PyTuple_New(Inner::n_kw + std::ranges::size(kwargs));
                        (
                            cpp_to_python<Is>(
                                kwnames,
                                array,
                                std::forward<Values>(values)...
                            ),
                            ...
                        );
                        Py_ssize_t npos = Inner::n - 1 - Inner::n_kw;
                        result = PyObject_Vectorcall(
                            func,
                            array,
                            npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            kwnames
                        );
                        for (size_t i = 1; i <= Inner::n; ++i) {
                            Py_XDECREF(array[i]);
                        }
                        delete[] array;

                    } else {
                        const auto& args = impl::unpack_arg<Inner::args_idx>(
                            std::forward<Values>(values)...
                        );
                        const auto& kwargs = impl::unpack_arg<Inner::kwargs_idx>(
                            std::forward<Values>(values)...
                        );
                        size_t nargs = Inner::n - 2 + std::ranges::size(args) + std::ranges::size(kwargs);
                        PyObject** array = new PyObject*[nargs + 1];
                        array[0] = nullptr;
                        PyObject* kwnames = PyTuple_New(Inner::n_kw + std::ranges::size(kwargs));
                        (
                            cpp_to_python<Is>(
                                kwnames,
                                array,
                                std::forward<Values>(values)...
                            ),
                            ...
                        );
                        size_t npos = Inner::n - 2 - Inner::n_kw + std::ranges::size(args);
                        result = PyObject_Vectorcall(
                            func,
                            array,
                            npos | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            kwnames
                        );
                        for (size_t i = 1; i <= Inner::n; ++i) {
                            Py_XDECREF(array[i]);
                        }
                        delete[] array;
                    }

                    // A null return value indicates an error
                    if (result == nullptr) {
                        Exception::from_python();
                    }
                    return result;  // will be None if the function returns void
                }(
                    std::make_index_sequence<Outer::n>{},
                    func,
                    std::forward<Values>(values)...
                );
            }

        };

    };

    /* Convert a non-member function pointer into a member function pointer of the
    given, cvref-qualified type.  Passing void as the enclosing class will return the
    non-member function pointer as-is. */
    template <typename Func, typename Self>
    struct _as_member_func {
        static constexpr bool enable = false;
    };
    template <typename R, typename... A, typename Self>
        requires (std::is_void_v<std::remove_cvref_t<Self>>)
    struct _as_member_func<R(*)(A...), Self> {
        static constexpr bool enable = true;
        using type = R(*)(A...);
    };
    template <typename R, typename... A, typename Self>
        requires (std::is_void_v<std::remove_cvref_t<Self>>)
    struct _as_member_func<R(*)(A...) noexcept, Self> {
        static constexpr bool enable = true;
        using type = R(*)(A...) noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...);
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) &;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) & noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) &&;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) && noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), const Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, const Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), const Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const &;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, const Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const & noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), const Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const &&;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, const Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const && noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), volatile Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, volatile Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), volatile Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile &;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, volatile Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile & noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), volatile Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile &&;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, volatile Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile && noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), const volatile Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, const volatile Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), const volatile Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile &;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, const volatile Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile & noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...), const volatile Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile &&;
    };
    template <typename R, typename... A, typename Self>
    struct _as_member_func<R(*)(A...) noexcept, const volatile Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile && noexcept;
    };
    template <typename Func, typename Self>
    using as_member_func = _as_member_func<Func, Self>::type;

    /* Introspect the proper signature for a py::Function instance from a generic
    function pointer. */
    template <typename T>
    struct Signature : BertrandTag {
        using type = T;
        static constexpr bool enable = false;
    };
    template <typename R, typename... A>
    struct Signature<R(*)(A...)> : Arguments<A...> {
        using type = R(*)(A...);
        static constexpr bool enable = true;
        static constexpr bool has_self = false;
        static constexpr bool return_is_convertible_to_python =
            std::convertible_to<R, Object>;
        using Return = R;
        using Self = void;
        using to_ptr = Signature;
        using to_value = Signature<R(A...)>;
        template <typename R2>
        using with_return = Signature<R2(*)(A...)>;
        template <typename C> requires (as_member_func<R(*)(A...), C>::enable)
        using with_self = Signature<as_member_func<R(*)(A...), C>>;
        template <typename... A2>
        using with_args = Signature<R(*)(A2...)>;
    };
    template <typename R, typename... A>
    struct Signature<R(*)(A...) noexcept> : Signature<R(*)(A...)> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...)> : Arguments<C&, A...> {
        using type = R(C::*)(A...);
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool return_is_convertible_to_python =
            std::convertible_to<R, Object>;
        using Return = R;
        using Self = C&;
        using to_ptr = Signature<R(*)(Self, A...)>;
        using to_value = Signature<R(Self, A...)>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...)>;
        template <typename C2> requires (as_member_func<R(*)(A...), C2>::enable)
        using with_self = Signature<as_member_func<R(*)(A...), C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...)>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) noexcept> : Signature<R(C::*)(A...)> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) &> : Signature<R(C::*)(A...)> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) & noexcept> : Signature<R(C::*)(A...)> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const> : Arguments<const C&, A...> {
        using type = R(C::*)(A...) const;
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool return_is_convertible_to_python =
            std::convertible_to<R, Object>;
        using Return = R;
        using Self = const C&;
        using to_ptr = Signature<R(*)(Self, A...)>;
        using to_value = Signature<R(Self, A...)>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) const>;
        template <typename C2> requires (as_member_func<R(*)(A...), C2>::enable)
        using with_self = Signature<as_member_func<R(*)(A...), C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const noexcept> : Signature<R(C::*)(A...) const> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const &> : Signature<R(C::*)(A...) const> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const & noexcept> : Signature<R(C::*)(A...) const> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) volatile> : Arguments<volatile C&, A...> {
        using type = R(C::*)(A...) volatile;
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool return_is_convertible_to_python =
            std::convertible_to<R, Object>;
        using Return = R;
        using Self = volatile C&;
        using to_ptr = Signature<R(*)(Self, A...)>;
        using to_value = Signature<R(Self, A...)>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) volatile>;
        template <typename C2> requires (as_member_func<R(*)(A...), C2>::enable)
        using with_self = Signature<as_member_func<R(*)(A...), C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) volatile>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) volatile noexcept> : Signature<R(C::*)(A...) volatile> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) volatile &> : Signature<R(C::*)(A...) volatile> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) volatile & noexcept> : Signature<R(C::*)(A...) volatile> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile> : Arguments<const volatile C&, A...> {
        using type = R(C::*)(A...) const volatile;
        static constexpr bool enable = true;
        static constexpr bool has_self = true;
        static constexpr bool return_is_convertible_to_python =
            std::convertible_to<R, Object>;
        using Return = R;
        using Self = const volatile C&;
        using to_ptr = Signature<R(*)(Self, A...)>;
        using to_value = Signature<R(Self, A...)>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) const volatile>;
        template <typename C2> requires (as_member_func<R(*)(A...), C2>::enable)
        using with_self = Signature<as_member_func<R(*)(A...), C2>>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const volatile>;
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile noexcept> : Signature<R(C::*)(A...) const volatile> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile &> : Signature<R(C::*)(A...) const volatile> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile & noexcept> : Signature<R(C::*)(A...) const volatile> {};

    /// TODO: I need a static parent class that serves as the template interface for
    /// all function types, and which implements __class_getitem__ to allow for
    /// similar class subscription syntax as for Python callables:
    ///
    /// Function[Foo: None, [bool, "x": int, "/", "y", "*args": float, "*", "z": str: ..., "**kwargs": type]]
    /// 
    /// This describes a bound method of Foo which returns void and takes a bool and
    /// an int as positional parameters, the second of which is explicitly named "x"
    /// (although this is ignored in the final key as a normalization).  It then
    /// takes a positional-or-keyword argument named "y" with any type, followed by
    /// an arbitrary number of positional arguments of type float.  Finally, it takes
    /// a keyword-only argument named "z" with a default value, and then
    /// an arbitrary number of keyword arguments pointing to type objects.

    /// TODO: one of these signatures is going to have to be generated for each
    /// function type, and must include at least a return type and parameter list,
    /// which may be Function[None, []] for a void function with no arguments.
    /// TODO: there has to also be some complex logic to make sure that type
    /// identifiers are compatible, possibly requiring an overload of isinstance()/
    /// issubclass() to ensure the same semantics are followed in both Python and C++.

    template <typename T>
    concept function_pointer_like = Signature<T>::enable;
    template <typename T>
    concept args_fit_within_bitset = Signature<T>::n <= 64;
    template <typename T>
    concept args_are_convertible_to_python = Signature<T>::args_are_convertible_to_python;
    template <typename T>
    concept return_is_convertible_to_python = Signature<T>::return_is_convertible_to_python;
    template <typename T>
    concept proper_argument_order = Signature<T>::proper_argument_order;
    template <typename T>
    concept no_duplicate_arguments = Signature<T>::no_duplicate_arguments;
    template <typename T>
    concept no_required_after_default = Signature<T>::no_required_after_default;

}


template <typename F = Object(*)(
    Arg<"args", const Object&>::args,
    Arg<"kwargs", const Object&>::kwargs
)>
    requires (
        impl::function_pointer_like<F> &&
        impl::args_fit_within_bitset<F> &&
        impl::args_are_convertible_to_python<F> &&
        impl::return_is_convertible_to_python<F> &&
        impl::proper_argument_order<F> &&
        impl::no_duplicate_arguments<F> &&
        impl::no_required_after_default<F>
    )
struct Function;


/// TODO: Signature's semantics have changed slightly, now all the conversions return
/// new Signature types, and Signature::type is used to get the underlying function
/// type.  This needs to be accounted for in the Function<> specializations, and there
/// needs to be a separate Function<> type for each Signature specialization, so that
/// type information is never lost.




/// TODO: I would also need some way to disambiguate static functions from member
/// functions when doing CTAD.  This is probably accomplished by providing an extra
/// argument to the constructor which holds the `self` value, and is implicitly
/// convertible to the function's first parameter type.  In that case, the CTAD
/// guide would always deduce to a member function over a static function.  If the
/// extra argument is given and is not convertible to the first parameter type, then
/// we issue a compile error, and if the extra argument is not given at all, then we
/// interpret it as a static function.


template <typename F>
struct Interface<Function<F>> : impl::FunctionTag {

    /* The normalized function pointer type for this specialization. */
    using Signature = impl::Signature<F>::type;

    /* The function's return type. */
    using Return = impl::Signature<F>::Return;

    /* The type of the function's `self` argument, or void if it is not a member
    function. */
    using Self = impl::Signature<F>::Self;

    /* A type holding the function's default values, which are inferred from the input
    signature and stored as a `std::tuple`. */
    using Defaults = impl::Signature<F>::Defaults;

    /* Instantiate a new function type with the same arguments, but a different return
    type. */
    template <typename R>
    using with_return = Function<typename impl::Signature<F>::template with_return<R>>;

    /* Instantiate a new function type with the same return type, but different
    arguments. */
    template <typename... A>
    using with_args = Function<typename impl::Signature<F>::template with_args<A...>>;

    /* Instantiate a new function type with the same return type and arguments, but
    bound to a particular type. */
    template <typename C>
    using with_self = Function<typename impl::Signature<F>::template with_self<C>>;

    /* Check whether a target function can be called with this function signature, i.e.
    that it can be invoked with this function's parameters and returns a type that
    can be converted to this function's return type. */
    template <typename Func>
    static constexpr bool compatible = impl::Signature<F>::template compatible<Func>;

    /* Check whether the function is invocable with the given arguments at
    compile-time. */
    template <typename... Args>
    static constexpr bool invocable = impl::Signature<F>::template Bind<Args...>::enable;

    /* The total number of arguments that the function accepts, not counting `self`. */
    static constexpr size_t n = impl::Signature<F>::n;

    /* The total number of positional arguments that the function accepts, counting
    both positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_pos = impl::Signature<F>::n_pos;

    /* The total number of positional-only arguments that the function accepts. */
    static constexpr size_t n_posonly = impl::Signature<F>::n_posonly;

    /* The total number of keyword arguments that the function accepts, counting
    both positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_kw = impl::Signature<F>::n_kw;

    /* The total number of keyword-only arguments that the function accepts. */
    static constexpr size_t n_kwonly = impl::Signature<F>::n_kwonly;

    /* The total number of optional arguments that are present in the function
    signature, including both positional and keyword arguments. */
    static constexpr size_t n_opt = impl::Signature<F>::n_opt;

    /* The total number of optional positional arguments that the function accepts,
    counting both positional-only and positional-or-keyword arguments, but not
    keyword-only or variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_opt_pos = impl::Signature<F>::n_opt_pos;

    /* The total number of optional positional-only arguments that the function
    accepts. */
    static constexpr size_t n_opt_posonly = impl::Signature<F>::n_opt_posonly;

    /* The total number of optional keyword arguments that the function accepts,
    counting both keyword-only and positional-or-keyword arguments, but not
    positional-only or variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_opt_kw = impl::Signature<F>::n_opt_kw;

    /* The total number of optional keyword-only arguments that the function
    accepts. */
    static constexpr size_t n_opt_kwonly = impl::Signature<F>::n_opt_kwonly;

    /* Check if the named argument is present in the function signature. */
    template <StaticStr Name>
    static constexpr bool has = impl::Signature<F>::template has<Name>;

    /* Check if the function accepts any positional arguments, counting both
    positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_pos = impl::Signature<F>::has_pos;

    /* Check if the function accepts any positional-only arguments. */
    static constexpr bool has_posonly = impl::Signature<F>::has_posonly;

    /* Check if the function accepts any keyword arguments, counting both
    positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_kw = impl::Signature<F>::has_kw;

    /* Check if the function accepts any keyword-only arguments. */
    static constexpr bool has_kwonly = impl::Signature<F>::has_kwonly;

    /* Check if the function accepts at least one optional argument. */
    static constexpr bool has_opt = impl::Signature<F>::has_opt;

    /* Check if the function accepts at least one optional positional argument.  This
    will match either positional-or-keyword or positional-only arguments. */
    static constexpr bool has_opt_pos = impl::Signature<F>::has_opt_pos;

    /* Check if the function accepts at least one optional positional-only argument. */
    static constexpr bool has_opt_posonly = impl::Signature<F>::has_opt_posonly;

    /* Check if the function accepts at least one optional keyword argument.  This will
    match either positional-or-keyword or keyword-only arguments. */
    static constexpr bool has_opt_kw = impl::Signature<F>::has_opt_kw;

    /* Check if the function accepts at least one optional keyword-only argument. */
    static constexpr bool has_opt_kwonly = impl::Signature<F>::has_opt_kwonly;

    /* Check if the function has a `self` parameter, indicating that it can be called
    as a member function. */
    static constexpr bool has_self = impl::Signature<F>::has_self;

    /* Check if the function accepts variadic positional arguments. */
    static constexpr bool has_args = impl::Signature<F>::has_args;

    /* Check if the function accepts variadic keyword arguments. */
    static constexpr bool has_kwargs = impl::Signature<F>::has_kwargs;

    /* Find the index of the named argument, if it is present. */
    template <StaticStr Name> requires (has<Name>)
    static constexpr size_t idx = impl::Signature<F>::template idx<Name>;

    /* Find the index of the first keyword argument that appears in the function
    signature.  This will match either a positional-or-keyword argument or a
    keyword-only argument.  If no such argument is present, this will return `n`. */
    static constexpr size_t kw_idx = impl::Signature<F>::kw_index;

    /* Find the index of the first keyword-only argument that appears in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t kwonly_idx = impl::Signature<F>::kw_only_index;

    /* Find the index of the first optional argument in the function signature.  If no
    such argument is present, this will return `n`. */
    static constexpr size_t opt_idx = impl::Signature<F>::opt_index;

    /* Find the index of the first optional positional argument in the function
    signature.  This will match either a positional-or-keyword argument or a
    positional-only argument.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_pos_idx = impl::Signature<F>::opt_pos_index;

    /* Find the index of the first optional positional-only argument in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_posonly_idx = impl::Signature<F>::opt_posonly_index;

    /* Find the index of the first optional keyword argument in the function signature.
    This will match either a positional-or-keyword argument or a keyword-only argument.
    If no such argument is present, this will return `n`. */
    static constexpr size_t opt_kw_idx = impl::Signature<F>::opt_kw_index;

    /* Find the index of the first optional keyword-only argument in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_kwonly_idx = impl::Signature<F>::opt_kwonly_index;

    /* Find the index of the variadic positional arguments in the function signature,
    if they are present.  If no such argument is present, this will return `n`. */
    static constexpr size_t args_idx = impl::Signature<F>::args_index;

    /* Find the index of the variadic keyword arguments in the function signature, if
    they are present.  If no such argument is present, this will return `n`. */
    static constexpr size_t kwargs_idx = impl::Signature<F>::kwargs_index;

    /* Get the (possibly annotated) type of the argument at index I of the function's
    signature. */
    template <size_t I> requires (I < n)
    using at = impl::Signature<F>::template at<I>;

    /* Get the type of the positional argument at index I. */
    template <size_t I> requires (I < args_idx && I < kwonly_idx)
    using Arg = impl::Signature<F>::template Arg<I>;

    /* Get the type of the named keyword argument. */
    template <StaticStr Name> requires (has<Name>)
    using Kwarg = impl::Signature<F>::template Kwarg<Name>;

    /* Get the type expected by the function's variadic positional arguments, or void
    if it does not accept variadic positional arguments. */
    using Args = impl::Signature<F>::Args;

    /* Get the type expected by the function's variadic keyword arguments, or void if
    it does not accept variadic keyword arguments. */
    using Kwargs = impl::Signature<F>::Kwargs;

    /* Call an external C++ function that matches the target signature using
    Python-style arguments.  The default values (if any) must be provided as an
    initializer list immediately before the function to be invoked, or constructed
    elsewhere and passed by reference.
    
    This helper has no runtime overhead over a traditional C++ function call, not
    counting any implicit logic when constructing default values. */
    template <typename Func, typename... Args>
        requires (compatible<Func> && invocable<Args...>)
    static Return invoke(const Defaults& defaults, Func&& func, Args&&... args) {
        using Call = impl::Signature<F>::template Bind<Args...>;
        return Call{}(
            defaults,
            std::forward<Func>(func),
            std::forward<Args>(args)...
        );
    }

    /* Call an external Python function using Python-style arguments.  The optional
    `R` template parameter specifies an interim return type, which is used to interpret
    the result of the raw CPython call.  The interim result will then be implicitly
    converted to the function's final return type.  The default value is `Object`,
    which incurs an additional dynamic type check on conversion.  If the exact return
    type is known in advance, then setting `R` equal to that type will avoid any extra
    checks or conversions at the expense of safety if that type is incorrect.  */
    template <typename R = Object, typename... Args> requires (invocable<Args...>)
    static Return invoke(PyObject* func, Args&&... args) {
        static_assert(
            !std::is_reference_v<R>,
            "Interim return type must not be a reference"
        );
        static_assert(
            std::derived_from<R, Object>,
            "Interim return type must be a subclass of Object"
        );

        /// TODO: this type check gets wierd.  I'll have to revisit it when I immerse
        /// myself in the type system again.  I need a way to traverse from
        /// `Type<Function>` to the parent template interface, so this has the lowest
        /// overhead possible.

        // // bypass the Python interpreter if the function is an instance of the coupled
        // // function type, which guarantees that the template signatures exactly match
        // if (PyType_IsSubtype(Py_TYPE(func), &PyFunction::type)) {
        //     PyFunction* func = reinterpret_cast<PyFunction*>(ptr(func));
        //     if constexpr (std::is_void_v<Return>) {
        //         invoke(
        //             func->defaults,
        //             func->func,
        //             std::forward<Source>(args)...
        //         );
        //     } else {
        //         return invoke(
        //             func->defaults,
        //             func->func,
        //             std::forward<Source>(args)...
        //         );
        //     }
        // }

        using Call = impl::Signature<F>::template Bind<Args...>;
        PyObject* result = Call{}(
            func,
            std::forward<Args>(args)...
        );

        // Python void functions return a reference to None, which we must release
        if constexpr (std::is_void_v<Return>) {
            Py_DECREF(result);
        } else {
            return reinterpret_steal<R>(result);
        }
    }

    /* Register an overload for this function. */
    void overload(const Function<F>& func) {

    }

    /* Attach the function as a bound method of a Python type. */
    template <typename T>
    void attach(Type<T>& type) {

    }

    /// TODO: index into a Function in order to resolve certain overloads.



    /// TODO: when getting and setting these properties, do I need to use an Attr
    /// proxy?


    __declspec(property(get=_get_name, put=_set_name)) std::string __name__;
    [[nodiscard]] std::string _get_name(this const auto& self);
    void _set_name(this auto& self, const std::string& name);

    __declspec(property(get=_get_doc, put=_set_doc)) std::string __doc__;
    [[nodiscard]] std::string _get_doc(this const auto& self);
    void _set_doc(this auto& self, const std::string& doc);

    __declspec(property(get=_get_qualname, put=_set_qualname))
        std::optional<std::string> __qualname__;
    [[nodiscard]] std::optional<std::string> _get_qualname(this const auto& self);
    void _set_qualname(this auto& self, const std::string& qualname);

    __declspec(property(get=_get_module, put=_set_module))
        std::optional<std::string> __module__;
    [[nodiscard]] std::optional<std::string> _get_module(this const auto& self);
    void _set_module(this auto& self, const std::string& module);

    __declspec(property(get=_get_code, put=_set_code)) std::optional<Code> __code__;
    [[nodiscard]] std::optional<Code> _get_code(this const auto& self);
    void _set_code(this auto& self, const Code& code);

    /// TODO: defined in __init__.h
    __declspec(property(get=_get_globals)) std::optional<Dict<Str, Object>> __globals__;
    [[nodiscard]] std::optional<Dict<Str, Object>> _get_globals(this const auto& self);

    __declspec(property(get=_get_closure)) std::optional<Tuple<Object>> __closure__;
    [[nodiscard]] std::optional<Tuple<Object>> _get_closure(this const auto& self);

    __declspec(property(get=_get_defaults, put=_set_defaults))
        std::optional<Tuple<Object>> __defaults__;
    [[nodiscard]] std::optional<Tuple<Object>> _get_defaults(this const auto& self);
    void _set_defaults(this auto& self, const Tuple<Object>& defaults);

    __declspec(property(get=_get_annotations, put=_set_annotations))
        std::optional<Dict<Str, Object>> __annotations__;
    [[nodiscard]] std::optional<Dict<Str, Object>> _get_annotations(this const auto& self);
    void _set_annotations(this auto& self, const Dict<Str, Object>& annotations);

    __declspec(property(get=_get_kwdefaults, put=_set_kwdefaults))
        std::optional<Dict<Str, Object>> __kwdefaults__;
    [[nodiscard]] std::optional<Dict<Str, Object>> _get_kwdefaults(this const auto& self);
    void _set_kwdefaults(this auto& self, const Dict<Str, Object>& kwdefaults);

    __declspec(property(get=_get_type_params, put=_set_type_params))
        std::optional<Tuple<Object>> __type_params__;
    [[nodiscard]] std::optional<Tuple<Object>> _get_type_params(this const auto& self);
    void _set_type_params(this auto& self, const Tuple<Object>& type_params);

    /// TODO: instance methods (given as pointer to member functions) support
    /// additional __self__ and __func__ properties?

};
template <typename F>
struct Interface<Type<Function<F>>> {

    /* The normalized function pointer type for this specialization. */
    using Signature = Interface<Function<F>>::Signature;

    /* The function's return type. */
    using Return = Interface<Function<F>>::Return;

    /* The type of the function's `self` argument, or void if it is not a member
    function. */
    using Self = Interface<Function<F>>::Self;

    /* A type holding the function's default values, which are inferred from the input
    signature and stored as a `std::tuple`. */
    using Defaults = Interface<Function<F>>::Defaults;

    /* The total number of arguments that the function accepts, not counting `self`. */
    static constexpr size_t n = Interface<Function<F>>::n;

    /* The total number of positional arguments that the function accepts, counting
    both positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_pos = Interface<Function<F>>::n_pos;

    /* The total number of positional-only arguments that the function accepts. */
    static constexpr size_t n_posonly = Interface<Function<F>>::n_posonly;

    /* The total number of keyword arguments that the function accepts, counting
    both positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_kw = Interface<Function<F>>::n_kw;

    /* The total number of keyword-only arguments that the function accepts. */
    static constexpr size_t n_kwonly = Interface<Function<F>>::n_kwonly;

    /* The total number of optional arguments that are present in the function
    signature, including both positional and keyword arguments. */
    static constexpr size_t n_opt = Interface<Function<F>>::n_opt;

    /* The total number of optional positional arguments that the function accepts,
    counting both positional-only and positional-or-keyword arguments, but not
    keyword-only or variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_opt_pos = Interface<Function<F>>::n_opt_pos;

    /* The total number of optional positional-only arguments that the function
    accepts. */
    static constexpr size_t n_opt_posonly = Interface<Function<F>>::n_opt_posonly;

    /* The total number of optional keyword arguments that the function accepts,
    counting both keyword-only and positional-or-keyword arguments, but not
    positional-only or variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_opt_kw = Interface<Function<F>>::n_opt_kw;

    /* The total number of optional keyword-only arguments that the function
    accepts. */
    static constexpr size_t n_opt_kwonly = Interface<Function<F>>::n_opt_kwonly;

    /* Check if the named argument is present in the function signature. */
    template <StaticStr Name>
    static constexpr bool has = Interface<Function<F>>::template has<Name>;

    /* Check if the function accepts any positional arguments, counting both
    positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_pos = Interface<Function<F>>::has_pos;

    /* Check if the function accepts any positional-only arguments. */
    static constexpr bool has_posonly = Interface<Function<F>>::has_posonly;

    /* Check if the function accepts any keyword arguments, counting both
    positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_kw = Interface<Function<F>>::has_kw;

    /* Check if the function accepts any keyword-only arguments. */
    static constexpr bool has_kwonly = Interface<Function<F>>::has_kwonly;

    /* Check if the function accepts at least one optional argument. */
    static constexpr bool has_opt = Interface<Function<F>>::has_opt;

    /* Check if the function accepts at least one optional positional argument.  This
    will match either positional-or-keyword or positional-only arguments. */
    static constexpr bool has_opt_pos = Interface<Function<F>>::has_opt_pos;

    /* Check if the function accepts at least one optional positional-only argument. */
    static constexpr bool has_opt_posonly = Interface<Function<F>>::has_opt_posonly;

    /* Check if the function accepts at least one optional keyword argument.  This will
    match either positional-or-keyword or keyword-only arguments. */
    static constexpr bool has_opt_kw = Interface<Function<F>>::has_opt_kw;

    /* Check if the function accepts at least one optional keyword-only argument. */
    static constexpr bool has_opt_kwonly = Interface<Function<F>>::has_opt_kwonly;

    /* Check if the function has a `self` parameter, indicating that it can be called
    as a member function. */
    static constexpr bool has_self = Interface<Function<F>>::has_self;

    /* Check if the function accepts variadic positional arguments. */
    static constexpr bool has_args = Interface<Function<F>>::has_args;

    /* Check if the function accepts variadic keyword arguments. */
    static constexpr bool has_kwargs = Interface<Function<F>>::has_kwargs;

    /* Find the index of the named argument, if it is present. */
    template <StaticStr Name> requires (has<Name>)
    static constexpr size_t idx = Interface<Function<F>>::template idx<Name>;

    /* Find the index of the first keyword argument that appears in the function
    signature.  This will match either a positional-or-keyword argument or a
    keyword-only argument.  If no such argument is present, this will return `n`. */
    static constexpr size_t kw_idx = Interface<Function<F>>::kw_index;

    /* Find the index of the first keyword-only argument that appears in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t kwonly_idx = Interface<Function<F>>::kw_only_index;

    /* Find the index of the first optional argument in the function signature.  If no
    such argument is present, this will return `n`. */
    static constexpr size_t opt_idx = Interface<Function<F>>::opt_index;

    /* Find the index of the first optional positional argument in the function
    signature.  This will match either a positional-or-keyword argument or a
    positional-only argument.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_pos_idx = Interface<Function<F>>::opt_pos_index;

    /* Find the index of the first optional positional-only argument in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_posonly_idx = Interface<Function<F>>::opt_posonly_index;

    /* Find the index of the first optional keyword argument in the function signature.
    This will match either a positional-or-keyword argument or a keyword-only argument.
    If no such argument is present, this will return `n`. */
    static constexpr size_t opt_kw_idx = Interface<Function<F>>::opt_kw_index;

    /* Find the index of the first optional keyword-only argument in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_kwonly_idx = Interface<Function<F>>::opt_kwonly_index;

    /* Find the index of the variadic positional arguments in the function signature,
    if they are present.  If no such argument is present, this will return `n`. */
    static constexpr size_t args_idx = Interface<Function<F>>::args_index;

    /* Find the index of the variadic keyword arguments in the function signature, if
    they are present.  If no such argument is present, this will return `n`. */
    static constexpr size_t kwargs_idx = Interface<Function<F>>::kwargs_index;

    /* Get the (possibly annotated) type of the argument at index I of the function's
    signature. */
    template <size_t I> requires (I < n)
    using at = Interface<Function<F>>::template at<I>;

    /* Get the type of the positional argument at index I. */
    template <size_t I> requires (I < args_idx && I < kwonly_idx)
    using Arg = Interface<Function<F>>::template Arg<I>;

    /* Get the type of the named keyword argument. */
    template <StaticStr Name> requires (has<Name>)
    using Kwarg = Interface<Function<F>>::template Kwarg<Name>;

    /* Get the type expected by the function's variadic positional arguments, or void
    if it does not accept variadic positional arguments. */
    using Args = Interface<Function<F>>::Args;

    /* Get the type expected by the function's variadic keyword arguments, or void if
    it does not accept variadic keyword arguments. */
    using Kwargs = Interface<Function<F>>::Kwargs;




    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::string __name__(const Self& self) {
        return self.__name__;
    }
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::string __doc__(const Self& self) {
        return self.__doc__;
    }
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<std::string> __qualname__(const Self& self) {
        return self.__qualname__;
    }
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<std::string> __module__(const Self& self) {
        return self.__module__;
    }
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<Code> __code__(const Self& self) {
        return self.__code__;
    }

    /// TODO: defined in __init__.h
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<Dict<Str, Object>> __globals__(const Self& self);
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<Tuple<Object>> __closure__(const Self& self);
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<Tuple<Object>> __defaults__(const Self& self);
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<Dict<Str, Object>> __annotations__(const Self& self);
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<Dict<Str, Object>> __kwdefaults__(const Self& self);
    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<Tuple<Object>> __type_params__(const Self& self);
};



/// TODO: all of these should be moved to their respective methods:
/// -   assert_matches() is needed in isinstance() + issubclass() to ensure
///     strict type safety.  Maybe also in the constructor, which can be
///     avoided using CTAD.
/// -   assert_satisfies() is needed in .overload()
/// -   callable()/assert_callable() is needed in __getitem__() and __call__()

struct TODO1 {

    template <size_t I, typename Container>
    static bool _matches(const Params<Container>& key) {
        using T = __object__<std::remove_cvref_t<typename ArgTraits<at<I>>::type>>::type;
        if (I < key.size()) {
            const Param& param = key[I];
            if constexpr (ArgTraits<at<I>>::kwonly()) {
                return (
                    (param.kwonly() & (param.opt() == ArgTraits<at<I>>::opt())) &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (reinterpret_cast<PyObject*>(param.type) == ptr(Type<T>()))
                );
            } else if constexpr (ArgTraits<at<I>>::kw()) {
                return (
                    (param.kw() & (param.opt() == ArgTraits<at<I>>::opt())) &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (reinterpret_cast<PyObject*>(param.type) == ptr(Type<T>()))
                );
            } else if constexpr (ArgTraits<at<I>>::pos()) {
                return (
                    (param.posonly() & (param.opt() == ArgTraits<at<I>>::opt())) &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (reinterpret_cast<PyObject*>(param.type) == ptr(Type<T>()))
                );
            } else if constexpr (ArgTraits<at<I>>::args()) {
                return (
                    param.args() &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (reinterpret_cast<PyObject*>(param.type) == ptr(Type<T>()))
                );
            } else if constexpr (ArgTraits<at<I>>::kwargs()) {
                return (
                    param.kwargs() &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (reinterpret_cast<PyObject*>(param.type) == ptr(Type<T>()))
                );
            } else {
                static_assert(false, "unrecognized parameter kind");
            }
        }
        return false;
    }

    template <size_t I, typename Container>
    static void _assert_matches(const Params<Container>& key) {
        using T = __object__<std::remove_cvref_t<typename ArgTraits<at<I>>::type>>::type;

        constexpr auto description = [](const Param& param) {
            if (param.kwonly()) {
                return "keyword-only";
            } else if (param.kw()) {
                return "positional-or-keyword";
            } else if (param.pos()) {
                return "positional";
            } else if (param.args()) {
                return "variadic positional";
            } else if (param.kwargs()) {
                return "variadic keyword";
            } else {
                return "<unknown>";
            }
        };

        if constexpr (ArgTraits<at<I>>::kwonly()) {
            if (I >= key.size()) {
                throw TypeError(
                    "missing keyword-only argument: '" + ArgTraits<at<I>>::name +
                    "' at index: " + std::to_string(I) 
                );
            }
            const Param& param = key[I];
            if (!param.kwonly()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be keyword-only, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected keyword-only argument '" + ArgTraits<at<I>>::name +
                    "' at index " + std::to_string(I) + ", not: '" +
                    std::string(param.name) + "'"
                );
            }
            if constexpr (ArgTraits<T>::opt()) {
                if (!param.opt()) {
                    throw TypeError(
                        "expected keyword-only argument '" + ArgTraits<at<I>>::name +
                        "' to have a default value"
                    );
                }
            } else {
                if (param.opt()) {
                    throw TypeError(
                        "expected keyword-only argument '" + ArgTraits<at<I>>::name +
                        "' to not have a default value"
                    );
                }
            }
            Type<T> expected;
            int rc = PyObject_IsSubclass(
                reinterpret_cast<PyObject*>(param.type),
                ptr(expected)
            );
            if (rc < 0) {
                Exception::from_python();
            } else if (!rc) {
                throw TypeError(
                    "expected keyword-only argument '" + ArgTraits<at<I>>::name +
                    "' to be a subclass of '" + repr(expected) + "', not: '" +
                    repr(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(param.type)
                    )) + "'"
                );
            }

        } else if constexpr (ArgTraits<at<I>>::kw()) {
            if (I >= key.size()) {
                throw TypeError(
                    "missing positional-or-keyword argument: '" +
                    ArgTraits<at<I>>::name + "' at index: " + std::to_string(I)
                );
            }
            const Param& param = key[I];
            if (!param.kw()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be positional-or-keyword, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected positional-or-keyword argument '" +
                    ArgTraits<at<I>>::name + "' at index " + std::to_string(I) +
                    ", not: '" + std::string(param.name) + "'"
                );
            }
            if constexpr (ArgTraits<T>::opt()) {
                if (!param.opt()) {
                    throw TypeError(
                        "expected positional-or-keyword argument '" +
                        ArgTraits<at<I>>::name + "' to have a default value"
                    );
                }
            } else {
                if (param.opt()) {
                    throw TypeError(
                        "expected positional-or-keyword argument '" +
                        ArgTraits<at<I>>::name + "' to not have a default value"
                    );
                }
            }
            Type<T> expected;
            int rc = PyObject_IsSubclass(
                reinterpret_cast<PyObject*>(param.type),
                ptr(expected)
            );
            if (rc < 0) {
                Exception::from_python();
            } else if (!rc) {
                throw TypeError(
                    "expected positional-or-keyword argument '" +
                    ArgTraits<at<I>>::name + "' to be a subclass of '" +
                    repr(expected) + "', not: '" + repr(
                        reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(param.type)
                        )
                    ) + "'"
                );
            }

        } else if constexpr (ArgTraits<at<I>>::pos()) {
            if (I >= key.size()) {
                throw TypeError(
                    "missing positional-only argument: '" +
                    ArgTraits<at<I>>::name + "' at index: " + std::to_string(I)
                );
            }
            const Param& param = key[I];
            if (!param.posonly()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be positional-only, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected positional-only argument '" +
                    ArgTraits<at<I>>::name + "' at index " + std::to_string(I) +
                    ", not: '" + std::string(param.name) + "'"
                );
            }
            if constexpr (ArgTraits<T>::opt()) {
                if (!param.opt()) {
                    throw TypeError(
                        "expected positional-only argument '" +
                        ArgTraits<at<I>>::name + "' to have a default value"
                    );
                }
            } else {
                if (param.opt()) {
                    throw TypeError(
                        "expected positional-only argument '" +
                        ArgTraits<at<I>>::name + "' to not have a default value"
                    );
                }
            }
            Type<T> expected;
            int rc = PyObject_IsSubclass(
                reinterpret_cast<PyObject*>(param.type),
                ptr(expected)
            );
            if (rc < 0) {
                Exception::from_python();
            } else if (!rc) {
                throw TypeError(
                    "expected positional-only argument '" +
                    ArgTraits<at<I>>::name + "' to be a subclass of '" +
                    repr(expected) + "', not: '" + repr(
                        reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(param.type)
                        )
                    ) + "'"
                );
            }

        } else if constexpr (ArgTraits<at<I>>::args()) {
            if (I >= key.size()) {
                throw TypeError(
                    "missing variadic positional argument: '" +
                    ArgTraits<at<I>>::name + "' at index: " + std::to_string(I)
                );
            }
            const Param& param = key[I];
            if (!param.args()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be variadic positional, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected variadic positional argument '" +
                    ArgTraits<at<I>>::name + "' at index " + std::to_string(I) +
                    ", not: '" + std::string(param.name) + "'"
                );
            }
            Type<T> expected;
            int rc = PyObject_IsSubclass(
                reinterpret_cast<PyObject*>(param.type),
                ptr(expected)
            );
            if (rc < 0) {
                Exception::from_python();
            } else if (!rc) {
                throw TypeError(
                    "expected variadic positional argument '" +
                    ArgTraits<at<I>>::name + "' to be a subclass of '" +
                    repr(expected) + "', not: '" + repr(
                        reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(param.type)
                        )
                    ) + "'"
                );
            }

        } else if constexpr (ArgTraits<at<I>>::kwargs()) {
            if (I >= key.size()) {
                throw TypeError(
                    "missing variadic keyword argument: '" +
                    ArgTraits<at<I>>::name + "' at index: " + std::to_string(I)
                );
            }
            const Param& param = key[I];
            if (!param.kwargs()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be variadic keyword, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected variadic keyword argument '" +
                    ArgTraits<at<I>>::name + "' at index " + std::to_string(I) +
                    ", not: '" + std::string(param.name) + "'"
                );
            }
            Type<T> expected;
            int rc = PyObject_IsSubclass(
                reinterpret_cast<PyObject*>(param.type),
                ptr(expected)
            );
            if (rc < 0) {
                Exception::from_python();
            } else if (!rc) {
                throw TypeError(
                    "expected variadic keyword argument '" +
                    ArgTraits<at<I>>::name + "' to be a subclass of '" +
                    repr(expected) + "', not: '" + repr(
                        reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(param.type)
                        )
                    ) + "'"
                );
            }

        } else {
            static_assert(false, "unrecognized parameter type");
        }
    }

    template <size_t I, typename... Ts>
    static constexpr bool _satisfies() { return true; };
    template <size_t I, typename T, typename... Ts>
    static constexpr bool _satisfies() {
        if constexpr (ArgTraits<at<I>>::kwonly()) {
            return (
                (
                    ArgTraits<T>::kwonly() &
                    (~ArgTraits<at<I>>::opt() | ArgTraits<T>::opt())
                ) &&
                (ArgTraits<at<I>>::name == ArgTraits<T>::name) &&
                issubclass<
                    typename ArgTraits<T>::type,
                    typename ArgTraits<at<I>>::type
                >()
            ) && satisfies<I + 1, Ts...>;

        } else if constexpr (ArgTraits<at<I>>::kw()) {
            return (
                (
                    ArgTraits<T>::kw() &
                    (~ArgTraits<at<I>>::opt() | ArgTraits<T>::opt())
                ) &&
                (ArgTraits<at<I>>::name == ArgTraits<T>::name) &&
                issubclass<
                    typename ArgTraits<T>::type,
                    typename ArgTraits<at<I>>::type
                >()
            ) && satisfies<I + 1, Ts...>;

        } else if constexpr (ArgTraits<at<I>>::pos()) {
            return (
                (
                    ArgTraits<T>::pos() &
                    (~ArgTraits<at<I>>::opt() | ArgTraits<T>::opt())
                ) &&
                (ArgTraits<at<I>>::name == ArgTraits<T>::name) &&
                issubclass<
                    typename ArgTraits<T>::type,
                    typename ArgTraits<at<I>>::type
                >()
            ) && satisfies<I + 1, Ts...>;

        } else if constexpr (ArgTraits<at<I>>::args()) {
            if constexpr ((ArgTraits<T>::pos() || ArgTraits<T>::args())) {
                if constexpr (
                    !issubclass<ArgTraits<T>::type, ArgTraits<at<I>>::type>()
                ) {
                    return false;
                }
                return satisfies<I, Ts...>;
            }
            return satisfies<I + 1, Ts...>;

        } else if constexpr (ArgTraits<at<I>>::kwargs()) {
            if constexpr (ArgTraits<T>::kw()) {
                if constexpr (
                    !has<ArgTraits<T>::name> &&
                    !issubclass<ArgTraits<T>::type, ArgTraits<at<I>>::type>()
                ) {
                    return false;
                }
                return satisfies<I, Ts...>;
            } else if constexpr (ArgTraits<T>::kwargs()) {
                if constexpr (
                    !issubclass<ArgTraits<T>::type, ArgTraits<at<I>>::type>()
                ) {
                    return false;
                }
                return satisfies<I, Ts...>;
            }
            return satisfies<I + 1, Ts...>;

        } else {
            static_assert(false, "unrecognized parameter type");
        }

        return false;
    }

    template <size_t I, typename Container>
    static bool _satisfies(const Params<Container>& key, size_t& idx) {
        using T = __object__<std::remove_cvref_t<typename ArgTraits<at<I>>::type>>::type;

        /// NOTE: if the original argument in the enclosing signature is required,
        /// then the new argument cannot be optional.  Otherwise, it can be either
        /// required or optional.

        if constexpr (ArgTraits<at<I>>::kwonly()) {
            if (idx < key.size()) {
                const Param& param = key[idx];
                ++idx;
                return (
                    (param.kwonly() & (~ArgTraits<at<I>>::opt() | param.opt())) &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (issubclass<T>(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(param.type))
                    ))
                );
            }

        } else if constexpr (ArgTraits<at<I>>::kw()) {
            if (idx < key.size()) {
                const Param& param = key[idx];
                ++idx;
                return (
                    (param.kw() & (~ArgTraits<at<I>>::opt() | param.opt())) &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (issubclass<T>(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(param.type))
                    ))
                );
            }

        } else if constexpr (ArgTraits<at<I>>::pos()) {
            if (idx < key.size()) {
                const Param& param = key[idx];
                ++idx;
                return (
                    (param.pos() & (~ArgTraits<at<I>>::opt() | param.opt())) &&
                    (param.name == ArgTraits<at<I>>::name) &&
                    (issubclass<T>(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(param.type))
                    ))
                );
            }

        } else if constexpr (ArgTraits<at<I>>::args()) {
            if (idx < key.size()) {
                const Param* param = &key[idx];
                while (param->pos()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(param->type))
                    )) {
                        return false;
                    }
                    ++idx;
                    if (idx == key.size()) {
                        return true;
                    }
                    param = &key[idx];            
                }
                if (param->args()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(param->type))
                    )) {
                        return false;
                    }
                    ++idx;
                    return true;
                }
            }
            return true;

        } else if constexpr (ArgTraits<at<I>>::kwargs()) {
            if (idx < key.size()) {
                const Param* param = &key[idx];
                while (param->kw()) {
                    if (
                        /// TODO: check to see if the argument is present
                        // !callback(param->name) &&
                        !issubclass<T>(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(param->type))
                    )) {
                        return false;
                    }
                    ++idx;
                    if (idx == key.size()) {
                        return true;
                    }
                    param = &key[idx];
                }
                if (param->kwargs()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(param->type))
                    )) {
                        return false;
                    }
                    ++idx;
                    return true;
                }
            }
            return true;

        } else {
            static_assert(false, "unrecognized parameter type");
        }
        return false;
    }

    template <size_t I, typename Container>
    static void _assert_satisfies(const Params<Container>& key, size_t& idx) {
        using T = __object__<std::remove_cvref_t<typename ArgTraits<at<I>>::type>>::type;

        constexpr auto description = [](const Param& param) {
            if (param.kwonly()) {
                return "keyword-only";
            } else if (param.kw()) {
                return "positional-or-keyword";
            } else if (param.pos()) {
                return "positional";
            } else if (param.args()) {
                return "variadic positional";
            } else if (param.kwargs()) {
                return "variadic keyword";
            } else {
                return "<unknown>";
            }
        };

        if constexpr (ArgTraits<at<I>>::kwonly()) {
            if (idx >= key.size()) {
                throw TypeError(
                    "missing keyword-only argument: '" + ArgTraits<at<I>>::name +
                    "' at index: " + std::to_string(idx)
                );
            }
            const Param& param = key[idx];
            if (!param.kwonly()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be keyword-only, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected keyword-only argument '" + ArgTraits<at<I>>::name +
                    "' at index " + std::to_string(idx) + ", not: '" +
                    std::string(param.name) + "'"
                );
            }
            if (~ArgTraits<at<I>>::opt() & param.opt()) {
                throw TypeError(
                    "required keyword-only argument '" + ArgTraits<at<I>>::name +
                    "' must not have a default value"
                );
            }
            if (!issubclass<T>(reinterpret_borrow<Object>(
                reinterpret_cast<PyObject*>(param.type))
            )) {
                throw TypeError(
                    "expected keyword-only argument '" + ArgTraits<at<I>>::name +
                    "' to be a subclass of '" + repr(Type<T>()) + "', not: '" +
                    repr(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(param.type)
                    )) + "'"
                );
            }
            ++idx;

        } else if constexpr (ArgTraits<at<I>>::kw()) {
            if (idx >= key.size()) {
                throw TypeError(
                    "missing positional-or-keyword argument: '" +
                    ArgTraits<at<I>>::name + "' at index: " + std::to_string(idx)
                );
            }
            const Param& param = key[idx];
            if (!param.kw()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be positional-or-keyword, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected positional-or-keyword argument '" +
                    ArgTraits<at<I>>::name + "' at index " + std::to_string(idx) +
                    ", not: '" + std::string(param.name) + "'"
                );
            }
            if (~ArgTraits<at<I>>::opt() & param.opt()) {
                throw TypeError(
                    "required positional-or-keyword argument '" +
                    ArgTraits<at<I>>::name + "' must not have a default value"
                );
            }
            if (!issubclass<T>(reinterpret_borrow<Object>(
                reinterpret_cast<PyObject*>(param.type))
            )) {
                throw TypeError(
                    "expected positional-or-keyword argument '" +
                    ArgTraits<at<I>>::name + "' to be a subclass of '" +
                    repr(Type<T>()) + "', not: '" + repr(
                        reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(param.type)
                        )
                    ) + "'"
                );
            }
            ++idx;

        } else if constexpr (ArgTraits<at<I>>::pos()) {
            if (idx >= key.size()) {
                throw TypeError(
                    "missing positional argument: '" + ArgTraits<at<I>>::name +
                    "' at index: " + std::to_string(idx)
                );
            }
            const Param& param = key[idx];
            if (!param.pos()) {
                throw TypeError(
                    "expected argument '" + ArgTraits<at<I>>::name +
                    "' to be positional, not " + description(param)
                );
            }
            if (param.name != ArgTraits<at<I>>::name) {
                throw TypeError(
                    "expected positional argument '" + ArgTraits<at<I>>::name +
                    "' at index " + std::to_string(idx) + ", not: '" +
                    std::string(param.name) + "'"
                );
            }
            if (~ArgTraits<at<I>>::opt() & param.opt()) {
                throw TypeError(
                    "required positional argument '" + ArgTraits<at<I>>::name +
                    "' must not have a default value"
                );
            }
            if (!issubclass<T>(reinterpret_borrow<Object>(
                reinterpret_cast<PyObject*>(param.type))
            )) {
                throw TypeError(
                    "expected positional argument '" + ArgTraits<at<I>>::name +
                    "' to be a subclass of '" + repr(Type<T>()) + "', not: '" +
                    repr(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(param.type)
                    )) + "'"
                );
            }
            ++idx;

        } else if constexpr (ArgTraits<at<I>>::args()) {
            if (idx < key.size()) {
                const Param* param = &key[idx];
                while (param->pos() && idx < key.size()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(param->type))
                    )) {
                        throw TypeError(
                            "expected positional argument '" +
                            std::string(param->name) + "' to be a subclass of '" +
                            repr(Type<T>()) + "', not: '" + repr(
                                reinterpret_borrow<Object>(
                                    reinterpret_cast<PyObject*>(param->type)
                                )
                            ) + "'"
                        );
                    }
                    ++idx;
                    if (idx == key.size()) {
                        return;
                    }
                    param = &key[idx];
                }
                if (param->args()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(param->type))
                    )) {
                        throw TypeError(
                            "expected variadic positional argument '" +
                            std::string(param->name) + "' to be a subclass of '" +
                            repr(Type<T>()) + "', not: '" + repr(
                                reinterpret_borrow<Object>(
                                    reinterpret_cast<PyObject*>(param->type)
                                )
                            ) + "'"
                        );
                    }
                    ++idx;
                }
            }

        } else if constexpr (ArgTraits<at<I>>::kwargs()) {
            if (idx < key.size()) {
                const Param* param = &key[idx];
                while (param->kw() && idx < key.size()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(param->type))
                    )) {
                        throw TypeError(
                            "expected keyword argument '" +
                            std::string(param->name) + "' to be a subclass of '" +
                            repr(Type<T>()) + "', not: '" + repr(
                                reinterpret_borrow<Object>(
                                    reinterpret_cast<PyObject*>(param->type)
                                )
                            ) + "'"
                        );
                    }
                    ++idx;
                    if (idx == key.size()) {
                        return;
                    }
                    param = &key[idx];
                }
                if (param->kwargs()) {
                    if (!issubclass<T>(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(param->type))
                    )) {
                        throw TypeError(
                            "expected variadic keyword argument '" +
                            std::string(param->name) + "' to be a subclass of '" +
                            repr(Type<T>()) + "', not: '" + repr(
                                reinterpret_borrow<Object>(
                                    reinterpret_cast<PyObject*>(param->type)
                                )
                            ) + "'"
                        );
                    }
                    ++idx;
                }
            }

        } else {
            static_assert(false, "unrecognized parameter type");
        }
    }

};


struct TODO2 {

    /* Check to see if a compile-time function signature exactly matches the
    enclosing parameter list. */
    template <typename... Params>
    static constexpr bool matches() {
        return (std::same_as<Params, Args> && ...);
    }

    /* Check to see if a dynamic function signature exactly matches the enclosing
    parameter list. */
    template <typename Container>
    static bool matches(const Params<Container>& key) {
        return []<size_t... Is>(
            std::index_sequence<Is...>,
            const Params<Container>& key
        ) {
            return key.size() == n && (_matches<Is>(key) && ...);
        }(std::make_index_sequence<n>{}, key);
    }

    /* Validate a dynamic function signature, raising an error if it does not
    exactly match the enclosing parameter list. */
    template <typename Container>
    static void assert_matches(const Params<Container>& key) {
        []<size_t... Is>(
            std::index_sequence<Is...>,
            const Params<Container>& key
        ) {
            if (key.size() != n) {
                throw TypeError(
                    "expected " + std::to_string(n) + " arguments, got " +
                    std::to_string(key.size())
                );
            }
            (_assert_matches<Is>(key), ...);
        }(std::make_index_sequence<n>{}, key);
    }

    /* Check to see if a compile-time function signature can be bound to the
    enclosing parameter list, meaning that it could be registered as a viable
    overload. */
    template <typename... Params>
    static constexpr bool satisfies() {
        return _satisfies<0, Params...>();
    }

    /* Check to see if a dynamic function signature can be bound to the enclosing
    parameter list, meaning that it could be registered as a viable overload. */
    template <typename Container>
    static bool satisfies(const Params<Container>& key) {
        return []<size_t... Is>(
            std::index_sequence<Is...>,
            const Params<Container>& key,
            size_t idx
        ) {
            return key.size() == n && (_satisfies<Is>(key, idx) && ...);
        }(std::make_index_sequence<n>{}, key, 0);
    }

    /* Validate a Python function signature, raising an error if it cannot be
    bound to the enclosing parameter list. */
    template <typename Container>
    static void assert_satisfies(const Params<Container>& key) {
        []<size_t... Is>(
            std::index_sequence<Is...>,
            const Params<Container>& key,
            size_t idx
        ) {
            if (key.size() != n) {
                throw TypeError(
                    "expected " + std::to_string(n) + " arguments, got " +
                    std::to_string(key.size())
                );
            }
            (_assert_satisfies<Is>(key, idx), ...);
        }(std::make_index_sequence<n>{}, key, 0);
    }

    /* Check to see if a set of compile-time call arguments satisfy the enclosing
    parameter list, accounting for default values, variadics, etc. */
    template <typename... Params>
    static constexpr bool callable() {
        return Bind<Params...>::template enable;
    }

    /* Check to see if a set of dynamic call arguments satisfy the enclosing
    parameter list, accounting for default values, variadics, etc. */
    template <typename Container>
    static bool callable(const Params<Container>& key) {
        size_t size = key.size();
        size_t idx = 0;
        uint64_t mask = 0;
        while (idx < size) {
            const Param& param = key[idx];
            if (param.name.empty()) {
                const Callback& callback = Arguments::callback(idx);
                if (!callback || !callback(param.type)) {
                    return false;
                }
                mask |= callback.mask;
            } else {
                const Callback& callback = Arguments::callback(param.name);
                if (!callback || mask & callback.mask || !callback(param.type)) {
                    return false;
                }
                mask |= callback.mask;
            }
            ++idx;
        }
        return mask == required && idx == size;
    }

    /* Validate a set of dynamic call arguments, raising an error if they do not
    satisfy the enclosing parameter list, accounting for default values, variadics,
    etc. */
    template <typename Container>
    static void assert_callable(const Params<Container>& key) {
        size_t size = key.size();
        size_t idx = 0;
        uint64_t mask = 0;
        while (idx < size) {
            const Param& param = key[idx];

            if (param.name.empty()) {
                const Callback& callback = Arguments::callback(idx);
                if (!callback) {
                    throw TypeError(
                        "received unexpected positional argument at index " +
                        std::to_string(idx) + ": '" + repr(param) + "'"
                    );
                }
                if (!callback(param.type)) {
                    throw TypeError(
                        "expected argument at index " + std::to_string(idx) +
                        "' to be a subclass of '" + repr(reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(callback.type())
                        )) + "', not: '" + repr(reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(param.type)
                        )) + "'"
                    );
                }
                mask |= callback.mask;

            } else {
                const Callback& callback = Arguments::callback(param.name);
                if (!callback) {
                    throw TypeError(
                        "received unexpected keyword argument: '" +
                        std::string(param.name) + "'"
                    );
                }
                if (mask & callback.mask) {
                    throw TypeError(
                        "received multiple values for argument '" +
                        std::string(param.name) + "'"
                    );
                }
                if (!callback(param.type)) {
                    throw TypeError(
                        "expected argument '" + std::string(param.name) +
                        "' to be a subclass of '" + repr(reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(callback.type())
                        )) + "', not: '" + repr(reinterpret_borrow<Object>(
                            reinterpret_cast<PyObject*>(param.type)
                        )) + "'"
                    );
                }
                mask |= callback.mask;
            }

            ++idx;
        }

        if (mask != required) {
            uint64_t missing = required & ~mask;
            std::string msg = "missing required arguments: [";
            size_t i = 0;
            while (i < n) {
                if (missing & (1ULL << i)) {
                    const Callback& param = positional_table[i];
                    if (param.name.empty()) {
                        msg += "<parameter " + std::to_string(i) + ">";
                    } else {
                        msg += "'" + std::string(param.name) + "'";
                    }
                    ++i;
                    break;
                }
                ++i;
            }
            while (i < n) {
                if (missing & (1ULL << i)) {
                    const Callback& param = positional_table[i];
                    if (param.name.empty()) {
                        msg += ", <parameter " + std::to_string(i) + ">";
                    } else {
                        msg += ", '" + std::string(param.name) + "'";
                    }
                }
                ++i;
            }
            msg += "]";
            throw TypeError(msg);
        }

        if (idx < size) {
            std::string msg = "received unexpected arguments: [";
            const Param& param = key[idx];
            if (param.name.empty()) {
                msg += "<parameter " + std::to_string(idx) + ">";
            } else {
                msg += "'" + std::string(param.name) + "'";
            }
            while (++idx < size) {
                const Param& param = key[idx];
                if (param.name.empty()) {
                    msg += ", <parameter " + std::to_string(idx) + ">";
                } else {
                    msg += ", '" + std::string(param.name) + "'";
                }
            }
            msg += "]";
            throw TypeError(msg);
        }
    }

};


struct TODO3 {


    /// TODO: OverloadKey should be deleted and its methods redistributed to
    /// py::Function.

    /// TODO: account for duplicate argument names when appending elements to the
    /// key?
    /// -> This is only necessary for the subscript operator, since the call
    /// operator will generate its keys via an index sequence over the template
    /// signature, and may have a more efficient way of handling this than a
    /// set-based lookup.  It's not necessary for function introspection either,
    /// since those arguments are always guaranteed to be unique.

    // /* An overload key consisting of a sequence of parameter annotations, which
    // describe a Python-style call signature.  Keys of this form can be used to search a
    // function's overload trie during calls and other operations. */
    // struct OverloadKey {
    // private:
    //     std::vector<Param> vec;
    //     Py_ssize_t idx = 0;
    //     Py_ssize_t posonly_idx = std::numeric_limits<size_t>::max();
    //     Py_ssize_t kw_idx = std::numeric_limits<size_t>::max();
    //     Py_ssize_t kwonly_idx = std::numeric_limits<size_t>::max();
    //     Py_ssize_t args_idx = std::numeric_limits<size_t>::max();
    //     Py_ssize_t kwargs_idx = std::numeric_limits<size_t>::max();
    //     Py_ssize_t default_idx = std::numeric_limits<size_t>::max();

    // public:
    //     size_t hash = 0;

    //     explicit OverloadKey(size_t size) {
    //         vec.reserve(size);
    //     }

    //     OverloadKey(OverloadKey&& other) noexcept = default;
    //     OverloadKey(const OverloadKey& other) :
    //         vec(other.vec),
    //         idx(other.idx),
    //         posonly_idx(other.posonly_idx),
    //         kw_idx(other.kw_idx),
    //         kwonly_idx(other.kwonly_idx),
    //         args_idx(other.args_idx),
    //         kwargs_idx(other.kwargs_idx),
    //         default_idx(other.default_idx),
    //         hash(other.hash)
    //     {
    //         vec.reserve(other.vec.capacity());
    //     }

    //     OverloadKey& operator=(OverloadKey&& other) noexcept = default;
    //     OverloadKey& operator=(const OverloadKey& other) {
    //         vec = other.vec;
    //         idx = other.idx;
    //         posonly_idx = other.posonly_idx;
    //         kw_idx = other.kw_idx;
    //         kwonly_idx = other.kwonly_idx;
    //         args_idx = other.args_idx;
    //         kwargs_idx = other.kwargs_idx;
    //         default_idx = other.default_idx;
    //         hash = other.hash;
    //         vec.reserve(other.vec.capacity());
    //         return *this;
    //     }

    //     /* Directly append a parameter to the key and update its hash accordingly. */
    //     void param(Param&& par) {
    //         hash = impl::hash_combine(hash, par.hash());
    //         vec[idx++] = std::move(par);
    //     }

    //     /* Append a parameter to the key, parsing it as if it were a hypothetical
    //     argument to a Python function.  This will throw an error if the parameter
    //     does not conform to Python calling conventions. */
    //     void getitem(PyObject* specifier) {
    //         std::string_view name;
    //         Param::Category category;
    //         PyTypeObject* type;

    //         // raw types are interpreted as positional arguments
    //         if (PyType_Check(specifier)) {
    //             if (idx > kw_idx) {
    //                 throw TypeError(
    //                     "positional argument cannot follow keyword argument: " +
    //                     repr(reinterpret_borrow<Object>(specifier))
    //                 );
    //             }
    //             category = Param::POS;
    //             type = reinterpret_cast<PyTypeObject*>(specifier);

    //         // slices are interpreted as keyword arguments
    //         } else if (PySlice_Check(specifier)) {
    //             PySliceObject* slice = reinterpret_cast<PySliceObject*>(specifier);
    //             if (!PyUnicode_Check(slice->start)) {
    //                 throw TypeError(
    //                     "first element of a slice must be a string representing a "
    //                     "keyword argument name, not: " +
    //                     repr(reinterpret_borrow<Object>(slice->start))
    //                 );
    //             }
    //             if (!PyType_Check(slice->stop)) {
    //                 throw TypeError(
    //                     "second element of a slice must be a type object representing "
    //                     "the hypothetical type of the argument, not: " +
    //                     repr(reinterpret_borrow<Object>(slice->stop))
    //                 );
    //             }
    //             name = Param::get_name(slice->start);
    //             category = Param::KW;
    //             type = reinterpret_cast<PyTypeObject*>(slice->stop);
    //             if (idx < kw_idx) {
    //                 kw_idx = idx;
    //             }

    //         // everything else is invalid
    //         } else {
    //             throw TypeError(
    //                 "expected a type for a positional argument or a slice for a "
    //                 "keyword argument, not: " +
    //                 repr(reinterpret_borrow<Object>(specifier))
    //             );
    //         }
    //         param({name, type, category});
    //     }

    //     /* Append a parameter to the key, parsing it as it it were provided to the
    //     function's Python-level `__class_getitem__()` method, as used to navigate the
    //     template hierarchy from Python.  This must fully specify the signature of a
    //     Python function, accounting for optional and variadic arguments, as well as
    //     Python-style delimiters.  An error will be raised if any of these are
    //     invalid. */
    //     void class_getitem(PyObject* specifier) {
    //         std::string_view name;
    //         Param::Category category;
    //         PyTypeObject* type;

    //         // raw types are interpreted as required, positional-only arguments
    //         if (PyType_Check(specifier)) {
    //             if (idx > posonly_idx) {
    //                 throw TypeError(
    //                     "positional-only argument cannot follow `/` delimiter: " +
    //                     repr(reinterpret_borrow<Object>(specifier))
    //                 );
    //             } else if (idx > kwonly_idx) {
    //                 throw TypeError(
    //                     "positional-only argument cannot follow '*' delimiter: " +
    //                     repr(reinterpret_borrow<Object>(specifier))
    //                 );
    //             } else if (idx > kw_idx) {
    //                 throw TypeError(
    //                     "positional-only argument cannot follow keyword argument: " +
    //                     repr(reinterpret_borrow<Object>(specifier))
    //                 );
    //             } else if (idx > args_idx) {
    //                 throw TypeError(
    //                     "positional-only argument cannot follow variadic positional "
    //                     "arguments " +
    //                     repr(reinterpret_borrow<Object>(specifier))
    //                 );
    //             } else if (idx > default_idx) {
    //                 throw TypeError(
    //                     "parameter without a default value cannot follow a parameter "
    //                     "with a default value: " +
    //                     repr(reinterpret_borrow<Object>(specifier))
    //                 );
    //             }
    //             category = Param::POS;
    //             type = reinterpret_cast<PyTypeObject*>(specifier);

    //         // slices are interpreted in several ways depending on their contents
    //         } else if (PySlice_Check(specifier)) {
    //             PySliceObject* slice = reinterpret_cast<PySliceObject*>(specifier);

    //             // If the first element is a type, then the second element must be
    //             // an ellipsis signifying a positional-only argument with a default
    //             // value.
    //             if (PyType_Check(slice->start)) {
    //                 if (idx > posonly_idx) {
    //                     throw TypeError(
    //                         "positional-only argument cannot follow `/` delimiter: " +
    //                         repr(reinterpret_borrow<Object>(specifier))
    //                     );
    //                 } else if (idx > kwonly_idx) {
    //                     throw TypeError(
    //                         "positional-only argument cannot follow '*' delimiter: " +
    //                         repr(reinterpret_borrow<Object>(specifier))
    //                     );
    //                 } else if (idx > kw_idx) {
    //                     throw TypeError(
    //                         "positional-only argument cannot follow keyword argument: " +
    //                         repr(reinterpret_borrow<Object>(specifier))
    //                     );
    //                 } else if (idx > args_idx) {
    //                     throw TypeError(
    //                         "positional-only argument cannot follow variadic "
    //                         "positional arguments " +
    //                         repr(reinterpret_borrow<Object>(specifier))
    //                     );
    //                 } else if (slice->stop != Py_Ellipsis) {
    //                     throw TypeError(
    //                         "positional-only argument with a default value must have "
    //                         "an ellipsis as the second element of the slice: " +
    //                         repr(reinterpret_borrow<Object>(specifier))
    //                     );
    //                 }
    //                 category = static_cast<Param::Category>(Param::POS | Param::DEFAULT);
    //                 type = reinterpret_cast<PyTypeObject*>(slice->start);
    //                 if (idx < default_idx) {
    //                     default_idx = idx;
    //                 }

    //             // If the first element is a string, then the second element must
    //             // be a type signifying a keyword or keyword-only argument,
    //             // depending on its position with respect to the "*" delimiter.
    //             // These may also have default values provided as an optional
    //             // third element.
    //             } else if (PyUnicode_Check(slice->start)) {
    //                 name = Param::get_name(slice->start);
    //                 if (!PyType_Check(slice->stop)) {
    //                     throw TypeError(
    //                         "argument type for parameter '" + std::string(name) +
    //                         "' must be a type object, not: " +
    //                         repr(reinterpret_borrow<Object>(slice->stop))
    //                     );
    //                 }
    //                 type = reinterpret_cast<PyTypeObject*>(slice->stop);

    //                 // if the parameter name starts with "**", then it signifies a
    //                 // variadic keyword argument
    //                 if (name.starts_with("**")) {
    //                     if (idx > kwargs_idx) {
    //                         throw TypeError(
    //                             "variadic keyword arguments appear at multiple "
    //                             "indices: " + std::to_string(kwargs_idx) +
    //                             " and " + std::to_string(idx)
    //                         );
    //                     } else if (slice->step != Py_None) {
    //                         throw TypeError(
    //                             "variadic keyword arguments cannot have default "
    //                             "values: " +
    //                             repr(reinterpret_borrow<Object>(specifier))
    //                         );
    //                     }
    //                     name.remove_prefix(2);
    //                     category = static_cast<Param::Category>(Param::KW | Param::VARIADIC);
    //                     kwargs_idx = idx;

    //                 // if the parameter name starts with "*", then it signifies a
    //                 // variadic positional argument
    //                 } else if (name.starts_with("*")) {
    //                     if (idx > args_idx) {
    //                         throw TypeError(
    //                             "variadic positional arguments appear at multiple "
    //                             "indices: " + std::to_string(args_idx) + " and " +
    //                             std::to_string(idx)
    //                         );
    //                     } else if (idx > kwonly_idx) {
    //                         throw TypeError(
    //                             "variadic positional arguments cannot follow "
    //                             "keyword-only argument delimiter '*': " +
    //                             repr(reinterpret_borrow<Object>(specifier))
    //                         );
    //                     } else if (slice->step != Py_None) {
    //                         throw TypeError(
    //                             "variadic positional arguments cannot have default "
    //                             "values: " +
    //                             repr(reinterpret_borrow<Object>(specifier))
    //                         );
    //                     }
    //                     name.remove_prefix(1);
    //                     category = static_cast<Param::Category>(Param::POS | Param::VARIADIC);
    //                     args_idx = idx;

    //                 // otherwise, it's a regular keyword argument
    //                 } else {
    //                     if (idx > kwargs_idx) {
    //                         throw TypeError(
    //                             "keyword argument cannot follow variadic keyword "
    //                             "arguments: " +
    //                             repr(reinterpret_borrow<Object>(specifier))
    //                         );
    //                     } else if (slice->step == Py_Ellipsis) {
    //                         if (idx > kwonly_idx) {
    //                             category = static_cast<Param::Category>(
    //                                 Param::KW | Param::DEFAULT
    //                             );
    //                         } else {
    //                             category = static_cast<Param::Category>(
    //                                 Param::POS | Param::KW | Param::DEFAULT
    //                             );
    //                             if (idx < default_idx) {
    //                                 default_idx = idx;
    //                             }
    //                         }
    //                     } else if (slice->step == Py_None) {
    //                         if (idx > kwonly_idx) {
    //                             category = Param::KW;
    //                         } else if (idx > default_idx) {
    //                             throw TypeError(
    //                                 "parameter without a default value cannot follow "
    //                                 "a parameter with a default value: " +
    //                                 repr(reinterpret_borrow<Object>(specifier))
    //                             );
    //                         } else {
    //                             category = static_cast<Param::Category>(
    //                                 Param::POS | Param::KW
    //                             );
    //                         }
    //                     } else {
    //                         throw TypeError(
    //                             "Keyword arguments with default values must use an "
    //                             "ellipsis as the third element, not: " +
    //                             repr(reinterpret_borrow<Object>(slice->step))
    //                         );
    //                     }
    //                     if (idx < kw_idx) {
    //                         kw_idx = idx;
    //                     }
    //                 }

    //             // everything else is invalid
    //             } else {
    //                 throw TypeError(
    //                     "expected the first index of a slice to be either a type for "
    //                     "a positional argument or a string for a keyword argument, "
    //                     "not: " + repr(reinterpret_borrow<Object>(specifier))
    //                 );
    //             }

    //         // raw strings are reserved for positional-only '/' or keyword-only '*'
    //         // delimiters
    //         } else if (PyUnicode_Check(specifier)) {
    //             Py_ssize_t len;
    //             const char* data = PyUnicode_AsUTF8AndSize(
    //                 specifier,
    //                 &len
    //             );
    //             if (data == nullptr) {
    //                 Exception::from_python();
    //             }
    //             name = {data, static_cast<size_t>(len)};

    //             // positional-only delimiter
    //             if (name == "/") {
    //                 if (idx > posonly_idx) {
    //                     throw TypeError(
    //                         "positional-only argument delimiter '/' appears at "
    //                         "multiple indices: " + std::to_string(posonly_idx) +
    //                         " and " + std::to_string(idx)
    //                     );
    //                 } else if (idx > kw_idx) {
    //                     throw TypeError(
    //                         "positional-only argument delimiter '/' cannot follow "
    //                         "keyword arguments at index"
    //                     );
    //                 } else if (idx > args_idx) {
    //                     throw TypeError(
    //                         "positional-only argument delimiter '/' cannot follow "
    //                         "variadic positional arguments"
    //                     );
    //                 } else if (idx > kwargs_idx) {
    //                     throw TypeError(
    //                         "positional-only argument delimiter '/' cannot follow "
    //                         "variadic keyword arguments"
    //                     );
    //                 }
    //                 posonly_idx = idx;
    //                 return;

    //             // keyword-only delimiter
    //             } else if (name == "*") {
    //                 if (idx > kwonly_idx) {
    //                     throw TypeError(
    //                         "keyword-only argument delimiter '*' appears at multiple "
    //                         "indices: " + std::to_string(kwonly_idx) + " and " +
    //                         std::to_string(idx)
    //                     );
    //                 } else if (idx > kwargs_idx) {
    //                     throw TypeError(
    //                         "keyword-only argument delimiter '*' cannot follow "
    //                         "variadic keyword arguments"
    //                     );
    //                 }
    //                 kwonly_idx = idx;
    //                 return;

    //             // everything else is invalid
    //             } else {
    //                 throw TypeError(
    //                     "raw strings are reserved for the '/' and '*' argument "
    //                     "delimiters, not: '" + std::string(name) + "'"
    //                 );
    //             }

    //         // everything else is invalid
    //         } else {
    //             throw TypeError(
    //                 "expected a type for a required positional-only argument; a slice "
    //                 "for a keyword argument, positional-only argument with a default "
    //                 "value, or a variadic parameter pack prefixed with '*' or '**'; "
    //                 "or a string for the '/' or '*' argument delimiters, not: " +
    //                 repr(reinterpret_borrow<Object>(specifier))
    //             );
    //         }
    //         param({name, type, category});
    //     }

    //     size_t size() const noexcept { return vec.size(); }
    //     bool empty() const noexcept { return vec.empty(); }
    //     auto& front() noexcept { return vec.front(); }
    //     auto& front() const noexcept { return vec.front(); }
    //     auto& back() noexcept { return vec.back(); }
    //     auto& back() const noexcept { return vec.back(); }
    //     auto& operator[](size_t i) noexcept { return vec[i]; }
    //     auto& operator[](size_t i) const noexcept { return vec[i]; }
    //     auto begin() noexcept { return vec.begin(); }
    //     auto begin() const noexcept { return vec.begin(); }
    //     auto cbegin() const noexcept { return vec.cbegin(); }
    //     auto end() noexcept { return vec.end(); }
    //     auto end() const noexcept { return vec.end(); }
    //     auto cend() const noexcept { return vec.cend(); }
    // };

};


/* A universal function wrapper that can represent either a Python function exposed to
C++, or a C++ function exposed to Python with equivalent semantics.  Supports keyword,
optional, and variadic arguments through the `py::Arg` annotation.

Notes
-----
When constructed with a C++ function, this class will create a Python object that
encapsulates the function and allows it to be called from Python.  The Python wrapper
has a unique type for each template signature, which allows Bertrand to enforce strong
type safety and provide accurate error messages if a signature mismatch is detected.
It also allows Bertrand to directly unpack the underlying function from the Python
object, bypassing the Python interpreter and demoting the call to pure C++ where
possible.  If the function accepts `py::Arg` annotations in its signature, then these
will be extracted using template metaprogramming and observed when the function is
called in either language.

When constructed with a Python function, this class will store the function directly
and allow it to be called from C++ with the same semantics as the Python interpreter.
The `inspect` module is used to extract parameter names, categories, and default
values, as well as type annotations if they are present, all of which will be checked
against the expected signature and result in errors if they do not match.  `py::Arg`
annotations can be used to provide keyword, optional, and variadic arguments according
to the templated signature, and the function will be called directly using the
vectorcall protocol, which is the most efficient way to call a Python function from
C++.  

Container unpacking via the `*` and `**` operators is also supported, although it must
be explicitly enabled for C++ containers by overriding the dereference operator (which
is done automatically for iterable Python objects), and is limited in some respects
compared to Python:

    1.  The unpacked container must be the last argument in its respective category
        (positional or keyword), and there can only be at most one of each at the call
        site.  These are not reflected in ordinary Python, but are necessary to ensure
        that compile-time argument matching is unambiguous.
    2.  The container's value type must be convertible to each of the argument types
        that follow it in the function signature, or else a compile error will be
        raised.
    3.  If double unpacking is performed, then the container must yield key-value pairs
        where the key is implicitly convertible to a string, and the value is
        convertible to the corresponding argument type.  If this is not the case, a
        compile error will be raised.
    4.  If the container does not contain enough elements to satisfy the remaining
        arguments, or it contains too many, a runtime error will be raised when the
        function is called.  Since it is impossible to know the size of the container
        at compile time, this cannot be done statically.

Examples
--------
Consider the following function:

    int subtract(int x, int y) {
        return x - y;
    }

We can directly wrap this as a `py::Function` if we want, which does not alter the
calling convention or signature in any way:

    py::Function func("subtract", "a simple example function", subtract);
    func(1, 2);  // returns -1

If this function is exported to Python, its call signature will remain unchanged,
meaning that both arguments must be supplied as positional-only arguments, and no
default values will be considered.

    >>> func(1, 2)  # ok, returns -1
    >>> func(1)  # error: missing required positional argument
    >>> func(1, y = 2)  # error: unexpected keyword argument

We can add parameter names and default values by annotating the C++ function (or a
wrapper around it) with `py::Arg` tags.  For instance:

    py::Function func(
        "subtract",
        "a simple example function",
        [](py::Arg<"x", int> x, py::Arg<"y", int>::opt y) {
            return subtract(x.value, y.value);
        },
        py::arg<"y"> = 2
    );

Note that the annotations store their values in an explicit `value` member, which uses
aggregate initialization to extend the lifetime of temporaries.  The annotations can
thus store references with the same semantics as an ordinary function call, as if the
annotations were not present.  For instance, this:

    py::Function func(
        "subtract",
        "a simple example function",
        [](py::Arg<"x", const int&> x, py::Arg<"y", const int&>::opt y) {
            return subtract(x.value, y.value);
        },
        py::arg<"y"> = 2
    );

is equivalent to the previous example in every way, but with the added benefit that the
`x` and `y` arguments will not be copied unnecessarily according to C++ value
semantics.

With this in place, we can now do the following:

    func(1);
    func(1, 2);
    func(1, py::arg<"y"> = 2);

    // or, equivalently:
    static constexpr auto x = py::arg<"x">;
    static constexpr auto y = py::arg<"y">;
    func(x = 1);
    func(x = 1, y = 2);
    func(y = 2, x = 1);  // keyword arguments can have arbitrary order

All of which will return the same result as before.  The function can also be passed to
Python and called similarly:

    >>> func(1)
    >>> func(1, 2)
    >>> func(1, y = 2)
    >>> func(x = 1)
    >>> func(x = 1, y = 2)
    >>> func(y = 2, x = 1)

What's more, all of the logic necessary to handle these cases is resolved statically at
compile time, meaning that there is no runtime cost for using these annotations, and no
additional code is generated for the function itself.  When it is called from C++, all
we have to do is inspect the provided arguments and match them against the underlying
signature, generating a compile time index sequence that can be used to reorder the
arguments and insert default values where needed.  In fact, each of the above
invocations will be transformed into the same underlying function call, with virtually
the same performance characteristics as raw C++ (disregarding any extra indirection
caused by the `std::function` wrapper).

Additionally, since all arguments are evaluated purely at compile time, we can enforce
strong type safety guarantees on the function signature and disallow invalid calls
using template constraints.  This means that proper call syntax is automatically
enforced throughout the codebase, in a way that allows static analyzers to give proper
syntax highlighting and LSP support. */
template <typename F>
    requires (
        impl::function_pointer_like<F> &&
        impl::args_fit_within_bitset<F> &&
        impl::args_are_convertible_to_python<F> &&
        impl::return_is_convertible_to_python<F> &&
        impl::proper_argument_order<F> &&
        impl::no_duplicate_arguments<F> &&
        impl::no_required_after_default<F>
    )
struct Function : Object, Interface<Function<F>> {
private:

    /* Describes a single node in the function's overload trie. */
    struct Node {
        /* Argument maps are topologically sorted such that parent classes are always
        checked after subclasses, ensuring that the most specific matching overload is
        always chosen. */
        struct Compare {
            static bool operator()(PyTypeObject* lhs, PyTypeObject* rhs) {
                int result = PyObject_IsSubclass(
                    reinterpret_cast<PyObject*>(lhs),
                    reinterpret_cast<PyObject*>(rhs)
                );
                if (result == -1) {
                    Exception::from_python();
                }
                return result;
            }
        };
        using TopoMap = std::map<PyTypeObject*, Node*, Compare>;

        /// TODO: perhaps I should delete the `alive` field and use an ordinary
        /// collection so that I can easily iterate over them

        TopoMap positional;
        std::unordered_map<std::string_view, TopoMap> keyword;
        TopoMap args;
        TopoMap kwargs;
        size_t alive;  // number of functions that are reachable from this node
        PyObject* func;  // can be null if this is not a terminal node

        /// TODO: the args and kwargs maps need special treatment to enforce
        /// consistency.  The first time I try to match against a variadic argument,
        /// I look it up in these maps and then force all subsequent arguments to
        /// match against the same type.  I can't check the args map more than once
        /// because that would allow potentially mixed *args types, which would
        /// otherwise trigger a backtracking search.

        Node(PyObject* func) : alive(0), func(Py_XNewRef(func)) {}
        Node(const Node&) = delete;
        Node(Node&&) = delete;
        ~Node() noexcept {
            for (auto [type, node] : positional) {
                delete node;
                Py_DECREF(type);
            }
            for (auto [name, map] : keyword) {
                for (auto& [type, node] : map) {
                    delete node;
                    Py_DECREF(type);
                }
            }
            for (auto [type, node] : args) {
                delete node;
                Py_DECREF(type);
            }
            for (auto [type, node] : kwargs) {
                delete node;
                Py_DECREF(type);
            }
            Py_XDECREF(func);
        }

        /// TODO: perhaps search() should raise an error if the key does not fully
        /// satisfy the function signature, regardless of whether a corresponding
        /// overload is found.

        /* Topologically search the overload trie for a matching signature, starting
        from the current node.  This will recursively backtrack until a matching node
        is found or the trie is exhausted, returning nullptr on a failed search.  The
        results will be cached within the PyFunction representation to bypass this
        method on subsequent invocations. */
        template <typename Container>
        Node* search(const impl::Params<Container>& key, size_t idx) const noexcept {
            // if we reach the end of the key, either return the current node if it is
            // terminal, or null to backtrack one level and continue searching
            if (idx == key.size()) {
                return func ? this : nullptr;
            }

            // iterate through a topological argument map, checking `issubclass()` at
            // every index, recurring if a match is found
            constexpr auto traverse = [](
                const impl::Params<Container>& key,
                size_t idx,
                const impl::Param& lookup,
                const TopoMap& map
            ) -> Node* {
                for (auto [type, node] : map) {
                    if (Compare{}(lookup.type, type)) {
                        // If a match is found, increment the index and recur.  If this
                        // returns nullptr, continue searching
                        Node* result = node->search(key, idx + 1);
                        if (result) {
                            return result;
                        }
                    }
                }
                return nullptr;
            };

            const impl::Param& lookup = key[idx];
            if (lookup.name.empty()) {
                return traverse(key, idx, lookup, positional);
            }
            auto it = keyword.find(lookup.name);
            if (it == keyword.end()) {
                return nullptr;
            }
            return traverse(key, idx, lookup, it->second);
        }

        /// TODO: insert() is going to have to be able to account for variadic
        /// arguments in the inserted function, which need special handling within the
        /// trie using some kind of self-referential node that consumes the remaining
        /// arguments in that category.  Kwargs may need to be represented as an empty
        /// key in the keyword map, which, if present, will be matched against the
        /// remaining keyword arguments in the call.  This will by definition have only
        /// a single child, which will be a self reference to the current node, such
        /// that all remaining keyword arguments will be consumed by the function.
        /// Perhaps the positional args can be handled similarly by just inserting a
        /// key into the positional map that holds a similar self-reference.
        /// -> Perhaps self references aren't great, since there could be other
        /// overloads that can tamper with this system and cause unexpected behavior.
        /// It might be better to store variadic args/kwargs separately in some way,
        /// perhaps as a separate maps in addition to the standard positional and
        /// keyword maps.

        /* Insert a function into the overload trie, or throw a TypeError if it
        conflicts with another node and would thus be ambiguous. */
        template <typename Container>
        void insert(const impl::Params<Container>& key, PyObject* func) {
            Node* curr = this;

            // iterate through a topological argument map, checking for exact type
            // identity at every index.  If a match is found, update curr to reflect
            // it, otherwise insert a new node and continue.
            constexpr auto traverse = [](
                TopoMap& map,
                Node*& curr,
                PyTypeObject* type,
                PyObject* func
            ) -> void {
                auto it = curr->positional.begin();
                auto end = curr->positional.end();
                while (it != end) {
                    if (it->first == type) {
                        curr = it->second;
                        return;
                    }
                    ++it;
                }
                /// TODO: insert a new node if no match is found
            };

            // iterate through the key, inserting each argument type into the trie
            // until we reach the final node, which will store the terminal function
            for (const impl::Param& lookup : key) {
                if (lookup.name.empty()) {
                    traverse(curr, curr->positional, lookup.type, func);
                } else {
                    auto it = curr->keyword.find(lookup.name);
                    if (it == curr->keyword.end()) {
                        /// TODO: insert a keyword with an empty map
                    }
                    traverse(curr, it->second, lookup.type, func);
                }
            }

            // if the final node already has a terminal function, then the overloads
            // are ambiguous, and we raise a runtime error.
            if (curr->func) {
                throw TypeError("overload already exists for this signature");
            }
            curr->func = Py_NewRef(func);
        }

        /* Remove a node from the overload trie and prune any dead-ends that lead to
        it.  Returns true if the node was found, or false otherwise. */
        template <typename Container>
        bool remove(const impl::Params<Container>& key, size_t idx) noexcept {
            // if we reach the end of the key, either return false if the current node
            // is not terminal, or clear the terminal function and prune the trie
            if (idx == key.size()) {
                if (func) {
                    PyObject* temp = func;
                    func = nullptr;
                    Py_DECREF(temp);
                    if (--alive == 0) {
                        delete this;
                    }
                    return true;
                } else {
                    return false;
                }
            }

            // iterate through a topological argument map, checking `issubclass()` at
            // every index, recurring if a match is found
            constexpr auto traverse = [](
                const impl::Params<Container>& key,
                size_t idx,
                const impl::Param& lookup,
                const TopoMap& map
            ) {
                for (auto [type, node] : map) {
                    if (Compare{}(lookup.type, type)) {
                        // if a match is found, increment the index and recur.  If this
                        // returns false, continue searching
                        bool result = node->remove(key, idx + 1);
                        if (result) {
                            if (--node->alive == 0) {
                                delete node;
                            }
                            return true;
                        }
                    }
                }
                return false;
            };

            const impl::Param& lookup = key[idx];
            if (lookup.name.empty()) {
                return traverse(key, idx, lookup, positional);
            }
            auto it = keyword.find(lookup.name);
            if (it == keyword.end()) {
                return false;
            }
            return traverse(key, idx, lookup, it->second);
        }

    };

    /// NOTE: The actual function type stored within the Python representation will
    /// always explicitly list the `self` parameter first in the case of member
    /// functions, following Python syntax.  This means that the internal function type
    /// can subtly differ from the public template signature, but the end result will
    /// be the same.  This translation is necessary to ensure consistent call behavior
    /// in both languages, and to simplify the internal logic as much as possible.

    /// NOTE: If the initializer can be converted to a function pointer (i.e. for
    /// stateless lambdas or raw function pointers), then we store it as such and
    /// avoid any overhead from `std::function`.  This means we only incur this overhead
    /// if a capturing lambda or other functor is used which does not provide an
    /// implicit conversion to a function pointer.  Using a tagged union to do this
    /// introduces some overhead of its own, but is negligible compared to the overhead
    /// of `std::function`. 

    /* Implementation for non-member functions. */
    template <typename Sig>
    struct PyFunction : def<PyFunction<Sig>, Function>, PyObject {
        static constexpr StaticStr __doc__ =
R"doc(A Python wrapper around a C++ function.

Notes
-----
This type is not directly instantiable from Python.  Instead, it can only be accessed
through the `bertrand.Function` template interface, which can be navigated by
subscripting the interface similar to a `Callable` type hint.

Examples
--------
>>> from bertrand import Function
>>> Function[None, [int, str]]
py::Function<void(*)(py::Int, py::Str)>
>>> Function[int, ["x": int, "y": int]]
py::Function<py::Int(*)(py::Arg<"x", py::Int>, py::Arg<"y", py::Int>)>
>>> Function[str, str, [str, ["maxsplit": int]]]
py::Function<py::Str(py::Str::*)(py::Str, py::Arg<"maxsplit", py::Int>::opt)>

As long as these types have been instantiated somewhere at the C++ level, then these
accessors will resolve to a unique Python type that wraps that instantiation.  If no
C++ instantiation could be found, then a `TypeError` will be raised.

Because each of these types is unique, they can be used with Python's `isinstance()`
and `issubclass()` functions to check against the exact template signature.  A global
check against the interface type will determine whether the function is implemented in
C++ (True) or Python (False).

TODO: explain how to properly subscript the interface and how arguments are translated
to a corresponding C++ function signature.

)doc";

        std::string name;
        std::string docstring;
        std::function<typename Sig::to_value::type> func;
        Sig::Defaults defaults;
        vectorcallfunc call;
        Node* root;
        size_t size;
        std::unordered_map<size_t, Node*> cache;  // value may be null

        PyFunction(
            std::string&& name,
            std::string&& docstring,
            std::function<typename Sig::to_value::type>&& func,
            Sig::Defaults&& defaults
        ) : name(std::move(name)),
            docstring(std::move(docstring)),
            func(std::move(func)),
            defaults(std::move(defaults)),
            call(&__call__),
            root(nullptr),
            size(0)
        {}

        ~PyFunction() {
            if (root) {
                delete root;
            }
        }

        template <StaticStr ModName>
        static Type<Function> __export__(Module<ModName> bindings) {
            /// TODO: manually initialize a heap type with a correct vectorcall offset.
            /// This has to be an instance of the metaclass, which may require a
            /// forward reference here.
            /// -> Perhaps this should be a pure static type, which would avoid an
            /// extra import statement whenever a function is called.  Same for the
            /// metaclass defined after this, which is used to distinguish between
            /// operands in the Python slots for binary operators.
        }

        /* Register an overload from Python. */
        static PyObject* overload(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) {

            self->cache.clear();
        }

        /* Attach the function to a type as a descriptor. */
        static PyObject* attach(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) {

        }

        /* Manually clear the function's internal overload cache from Python. */
        static PyObject* flush(PyFunction* self) {
            self->cache.clear();
            Py_RETURN_NONE;
        }

        /* Call the function from Python. */
        static PyObject* __call__(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) {
            // try {
            //     return []<size_t... Is>(
            //         std::index_sequence<Is...>,
            //         PyFunction* self,
            //         PyObject* const* args,
            //         Py_ssize_t nargsf,
            //         PyObject* kwnames
            //     ) {
            //         size_t nargs = PyVectorcall_NARGS(nargsf);
            //         if constexpr (!target::has_args) {
            //             constexpr size_t expected = target::size - target::kw_only_count;
            //             if (nargs > expected) {
            //                 throw TypeError(
            //                     "expected at most " + std::to_string(expected) +
            //                     " positional arguments, but received " +
            //                     std::to_string(nargs)
            //                 );
            //             }
            //         }
            //         size_t kwcount = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
            //         if constexpr (!target::has_kwargs) {
            //             for (size_t i = 0; i < kwcount; ++i) {
            //                 Py_ssize_t length;
            //                 const char* name = PyUnicode_AsUTF8AndSize(
            //                     PyTuple_GET_ITEM(kwnames, i),
            //                     &length
            //                 );
            //                 if (name == nullptr) {
            //                     Exception::from_python();
            //                 } else if (!target::has_keyword(name)) {
            //                     throw TypeError(
            //                         "unexpected keyword argument '" +
            //                         std::string(name, length) + "'"
            //                     );
            //                 }
            //             }
            //         }
            //         if constexpr (std::is_void_v<Return>) {
            //             self->func(
            //                 {impl::Arguments<Target...>::template from_python<Is>(
            //                     self->defaults,
            //                     args,
            //                     nargs,
            //                     kwnames,
            //                     kwcount
            //                 )}...
            //             );
            //             Py_RETURN_NONE;
            //         } else {
            //             return release(as_object(
            //                 self->func(
            //                     {impl::Arguments<Target...>::template from_python<Is>(
            //                         self->defaults,
            //                         args,
            //                         nargs,
            //                         kwnames,
            //                         kwcount
            //                     )}...
            //                 )
            //             ));
            //         }
            //     }(
            //         std::make_index_sequence<target::size>{},
            //         self,
            //         args,
            //         nargsf,
            //         kwnames
            //     );
            // } catch (...) {
            //     Exception::to_python();
            //     return nullptr;
            // }
        }

        /// TODO: maybe I should raise an error if the index does not fully satisfy the
        /// function's signature?
        /// -> Yes, and potentially __getitem__ should return a self reference.
        /// use __contains__ if you want to check for the presence of an overload.



        /// TODO: move this stuff up to Parameters, so that it can be easily accessed
        /// from the outside.


        /* Index the function to resolve a specific overload, as if the function were
        being called normally.  Returns `None` if no overload can be found, indicating
        that the function will be called with its base implementation. */
        static PyObject* __getitem__(PyFunction* self, PyObject* key) {



            /// TODO: before doing anything else, convert the key to a vector of
            /// parameter and then check whether or not it is valid for the function
            /// signature.  If not, raise an error.  Otherwise, continue as normal.

            if (!self->root) {
                return Py_NewRef(self);
            }



            // paths through the trie are cached to avoid redundant searches.  If no
            // match is found, then the cache will store a null pointer, which gets
            // translated to None and indicates that the function will be called with
            // its base implementation, and not a custom overload.
            constexpr auto get = [](
                const std::vector<Lookup>& vec,
                size_t hash,
                PyFunction* self,
                PyObject* key
            ) -> PyObject* {
                auto it = self->cache.find(hash);
                if (it != self->cache.end()) {
                    return Py_NewRef(it->second ? it->second->func : Py_None);
                }
                Node* node = self->root->search(vec, 0);
                self->cache[hash] = node;
                return Py_NewRef(node ? node->func : Py_None);
            };

            try {
                // if the key is a scalar, then we analyze it as a single argument and
                // avoid unnecessary allocations
                if (!PyTuple_Check(key)) {
                    std::vector<Lookup> vec = {resolve(key)};
                    size_t hash = impl::hash_combine(
                        std::hash<std::string_view>{}(vec.front().first),
                        reinterpret_cast<size_t>(vec.front().second)
                    );
                    return get(vec, hash, self, key);
                }

                // otherwise, the key is a tuple, and we need to construct an argument
                // vector from it in order to search the trie
                Py_ssize_t size = PyTuple_GET_SIZE(key);
                std::vector<Lookup> vec;
                vec.reserve(size);
                size_t hash = 0;
                for (Py_ssize_t i = 0; i < size; ++i) {
                    vec[i] = resolve(PyTuple_GET_ITEM(key, i));
                    const Lookup& lookup = vec[i];
                    hash = impl::hash_combine(
                        hash,
                        std::hash<std::string_view>{}(lookup.first),
                        reinterpret_cast<size_t>(lookup.second)
                    );
                }
                return get(vec, hash, self, key);

            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Delete a matching overload, removing it from the overload trie. */
        static int __delitem__(PyFunction* self, PyObject* key, PyObject* value) {
            if (value) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "functions do not support item assignment: use "
                    "`@func.overload` to register an overload instead"
                );
                return -1;
            }

            /// TODO: generate a lookup key from the Python object, and then
            /// call self->root->remove() to delete the overload.

            self->cache.clear();
        }

        /* Check whether a given function is contained within the overload trie. */
        static int __contains__(PyFunction* self, PyObject* func) {
            /// TODO: should take a function
        }

        /* `len(function)` will get the number of overloads that are currently being
        tracked. */
        static Py_ssize_t __len__(PyFunction* self) {
            return self->size;
        }

        /// TODO: turns out I can make all functions introspectable via Python's
        /// `inspect` module by adding a __signature__ attribute, although this is
        /// subject to change, and I have to check against the Python source code.
        /// -> __signature__ should be a getset descriptor that returns an
        /// inspect.Signature object from the function's signature.

        /* Default `repr()` demangles the function name + signature. */
        static PyObject* __repr__(PyFunction* self) {
            static const std::string demangled =
                impl::demangle(typeid(Function<F>).name());
            std::string s = "<" + demangled + " at " +
                std::to_string(reinterpret_cast<size_t>(self)) + ">";
            PyObject* string = PyUnicode_FromStringAndSize(s.c_str(), s.size());
            if (string == nullptr) {
                return nullptr;
            }
            return string;
        }

        /* Supplying a __signature__ attribute allows C++ functions to be introspected
        via the `inspect` module, just like their pure-Python equivalents. */
        static PyObject* __signature__(PyFunction* self, void* /* unused */) {
            try {
                Object inspect = reinterpret_steal<Object>(PyImport_Import(
                    impl::TemplateString<"inspect">::ptr
                ));
                if (inspect.is(nullptr)) {
                    return nullptr;
                }
                Object Signature = getattr<"Signature">(inspect);
                Object Parameter = getattr<"Parameter">(inspect);

                // build the parameter annotations
                Object tuple = PyTuple_New(Sig::n);
                if (tuple.is(nullptr)) {
                    return nullptr;
                }
                []<size_t... Is>(
                    std::index_sequence<Is...>,
                    PyObject* tuple,
                    PyFunction* self,
                    const Object& Parameter
                ) {
                    (PyTuple_SET_ITEM(  // steals a reference
                        tuple,
                        Is,
                        release(build_parameter<Is>(self, Parameter))
                    ), ...);
                }(
                    std::make_index_sequence<Sig::n>{},
                    ptr(tuple),
                    self,
                    Parameter
                );

                // get the return annotation
                Type<typename Sig::Return> return_type;

                // create the signature object
                PyObject* args[2] = {ptr(tuple), ptr(return_type)};
                Object kwnames = reinterpret_steal<Object>(
                    PyTuple_Pack(1, impl::TemplateString<"return_annotation">::ptr)
                );
                return PyObject_Vectorcall(
                    ptr(Signature),
                    args,
                    2,
                    ptr(kwnames)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        template <size_t I>
        static Object build_parameter(PyFunction* self, const Object& Parameter) {
            using T = Sig::template at<I>;
            using Traits = impl::ArgTraits<T>;

            Object name = reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(
                    Traits::name,
                    Traits::name.size()
                )
            );
            if (name.is(nullptr)) {
                Exception::from_python();
            }

            Object kind;
            if constexpr (Traits::kwonly()) {
                kind = getattr<"KEYWORD_ONLY">(Parameter);
            } else if constexpr (Traits::kw()) {
                kind = getattr<"POSITIONAL_OR_KEYWORD">(Parameter);
            } else if constexpr (Traits::pos()) {
                kind = getattr<"POSITIONAL_ONLY">(Parameter);
            } else if constexpr (Traits::args()) {
                kind = getattr<"VAR_POSITIONAL">(Parameter);
            } else if constexpr (Traits::kwargs()) {
                kind = getattr<"VAR_KEYWORD">(Parameter);
            } else {
                throw TypeError("unrecognized argument kind");
            }

            Object default_value = self->defaults.template get<I>();
            Type<typename Traits::type> annotation;

            PyObject* args[4] = {
                ptr(name),
                ptr(kind),
                ptr(default_value),
                ptr(annotation),
            };
            Object kwnames = reinterpret_steal<Object>(
                PyTuple_Pack(4,
                    impl::TemplateString<"name">::ptr,
                    impl::TemplateString<"kind">::ptr,
                    impl::TemplateString<"default">::ptr,
                    impl::TemplateString<"annotation">::ptr
                )
            );
            Object result = reinterpret_steal<Object>(PyObject_Vectorcall(
                ptr(Parameter),
                args,
                4,
                ptr(kwnames)
            ));
            if (result.is(nullptr)) {
                Exception::from_python();
            }
            return result;
        }

        inline static PyMethodDef methods[] = {

        };

        inline static PyGetSetDef getset[] = {
            {
                "__signature__",
                reinterpret_cast<getter>(&__signature__),
                nullptr,
                PyDoc_STR(
R"doc(A property that produces an accurate `inspect.Signature` object when a
C++ function is introspected from Python.

Notes
-----
Providing this descriptor allows the `inspect` module to be used on C++
functions as if they were implemented in Python itself, reflecting the signature
of their underlying `py::Function` representation.)doc"
                ),
                nullptr
            }
        };

    };

    /* A specialization for member functions expecting a `self` parameter, which can
    be supplied either during construction or dynamically at the call site according to
    Python syntax. */
    template <typename Sig> requires (Sig::has_self)
    struct PyFunction<Sig> : def<PyFunction<Sig>, Function>, PyObject {
        static constexpr StaticStr __doc__ =
R"doc(TODO: another docstring describing member functions/methods, and how they
differ from non-member functions.)doc";;

        std::optional<std::remove_reference_t<typename Sig::Self>> self;
        std::string name;
        std::string docstring;
        const bool is_ptr;
        union {
            Sig::to_ptr::type func_ptr;
            std::function<typename Sig::to_value::type> func;
        };
        Sig::Defaults defaults;
        vectorcallfunc call;
        Node* root;
        size_t size;

        PyFunction(
            std::string&& name,
            std::string&& docstring,
            Sig::to_ptr::type func,
            Sig::Defaults&& defaults
        ) : self(std::nullopt),
            name(std::move(name)),
            docstring(std::move(docstring)),
            is_ptr(true),
            func_ptr(func),
            defaults(std::move(defaults)),
            call(&__call__),
            root(nullptr),
            size(0)
        {}

        PyFunction(
            std::remove_reference_t<typename Sig::Self>&& self,
            std::string&& name,
            std::string&& docstring,
            Sig::to_ptr::type func,
            Sig::Defaults&& defaults
        ) : self(std::move(self)),
            name(std::move(name)),
            docstring(std::move(docstring)),
            is_ptr(true),
            func_ptr(func),
            defaults(std::move(defaults)),
            call(&__call__),
            root(nullptr),
            size(0)
        {}

        PyFunction(
            std::string&& name,
            std::string&& docstring,
            std::function<typename Sig::to_value::type>&& func,
            Sig::Defaults&& defaults
        ) : self(std::nullopt),
            name(std::move(name)),
            docstring(std::move(docstring)),
            is_ptr(false),
            func(std::move(func)),
            defaults(std::move(defaults)),
            call(&__call__),
            root(nullptr),
            size(0)
        {}

        PyFunction(
            std::remove_reference_t<typename Sig::Self>&& self,
            std::string&& name,
            std::string&& docstring,
            std::function<typename Sig::to_value::type>&& func,
            Sig::Defaults&& defaults
        ) : self(std::move(self)),
            name(std::move(name)),
            docstring(std::move(docstring)),
            is_ptr(false),
            func(std::move(func)),
            defaults(std::move(defaults)),
            call(&__call__),
            root(nullptr),
            size(0)
        {}

        ~PyFunction() {
            if (root) {
                delete root;
            }
            if (!is_ptr) {
                func.~function();
            }
        }

        static PyObject* __call__(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) {

        }

    };

public:
    using __python__ = PyFunction<impl::Signature<F>>;

    Function(PyObject* p, borrowed_t t) : Object(p, t) {}
    Function(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename... A> requires (implicit_ctor<Function>::template enable<A...>)
    Function(A&&... args) : Object(
        implicit_ctor<Function>{},
        std::forward<A>(args)...
    ) {}

    template <typename... A> requires (explicit_ctor<Function>::template enable<A...>)
    explicit Function(A&&... args) : Object(
        explicit_ctor<Function>{},
        std::forward<A>(args)...
    ) {}

};



/// TODO: class methods can be indicated by a member method of Type<T>.  That
/// would allow this mechanism to scale arbitrarily.


// TODO: constructor should fail if the function type is a subclass of my root
// function type, but not a subclass of this specific function type.  This
// indicates a type mismatch in the function signature, which is a violation of
// static type safety.  I can then print a helpful error message with the demangled
// function types which can show their differences.
// -> This can be implemented in the actual call operator itself, but it would be
// better to implement it on the constructor side.  Perhaps including it in the
// isinstance()/issubclass() checks would be sufficient, since those are used
// during implicit conversion anyways.


/// TODO: constructing a function from a callable should normalize to the correct
/// function pointer type rather than specializing on the callable type.  That will
/// require me to get the operator() type as a non-member function pointer.


template <
    impl::is_callable_any Func,
    typename... D
> requires (!impl::python_like<Func>)
Function(
    Func&&,
    typename impl::Signature<std::remove_reference_t<Func>>::Defaults&&...
) -> Function<typename impl::Signature<std::remove_reference_t<Func>>::type>;


template <impl::is_callable_any F, typename... D> requires (!impl::python_like<F>)
Function(std::string, F, D...) -> Function<
    typename impl::Signature<std::decay_t<F>>::type
>;


template <impl::is_callable_any F, typename... D> requires (!impl::python_like<F>)
Function(std::string, std::string, F, D...) -> Function<
    typename impl::Signature<std::decay_t<F>>::type
>;


namespace impl {
    /// NOTE: the type returned by `std::mem_fn()` is implementation-defined, so we
    /// have to do some template magic to trick the compiler into deducing the correct
    /// type during template specializations.
    template <typename Sig>
    using std_mem_fn_type = respecialize<decltype(
        std::mem_fn(std::declval<void(Object::*)()>())
    ), Sig>;
};


#define NON_MEMBER_FUNC(IN, OUT) \
    template <typename R, typename... A> \
    struct __object__<IN> : Returns<Function<OUT>> {};

#define MEMBER_FUNC(IN, OUT) \
    template <typename R, typename C, typename... A> \
    struct __object__<IN> : Returns<Function<OUT>> {};

#define STD_MEM_FN(IN, OUT) \
    template <typename R, typename C, typename... A> \
    struct __object__<impl::std_mem_fn_type<IN>> : Returns<Function<OUT>> {};


NON_MEMBER_FUNC(R(A...), R(*)(A...))
NON_MEMBER_FUNC(R(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(&&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(&&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*&&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*&&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*const)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*const)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*const&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*const&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*const&&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*const&&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*volatile)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*volatile)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*volatile&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*volatile&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*volatile&&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*volatile&&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*const volatile)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*const volatile)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*const volatile&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*const volatile&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(R(*const volatile&&)(A...), R(*)(A...))
NON_MEMBER_FUNC(R(*const volatile&&)(A...) noexcept, R(*)(A...) noexcept)
NON_MEMBER_FUNC(std::function<R(A...)>, R(*)(A...))
NON_MEMBER_FUNC(std::function<R(A...)>&, R(*)(A...))
NON_MEMBER_FUNC(std::function<R(A...)>&&, R(*)(A...))
NON_MEMBER_FUNC(const std::function<R(A...)>, R(*)(A...))
NON_MEMBER_FUNC(const std::function<R(A...)>&, R(*)(A...))
NON_MEMBER_FUNC(const std::function<R(A...)>&&, R(*)(A...))
NON_MEMBER_FUNC(volatile std::function<R(A...)>, R(*)(A...))
NON_MEMBER_FUNC(volatile std::function<R(A...)>&, R(*)(A...))
NON_MEMBER_FUNC(volatile std::function<R(A...)>&&, R(*)(A...))
NON_MEMBER_FUNC(const volatile std::function<R(A...)>, R(*)(A...))
NON_MEMBER_FUNC(const volatile std::function<R(A...)>&, R(*)(A...))
NON_MEMBER_FUNC(const volatile std::function<R(A...)>&&, R(*)(A...))
MEMBER_FUNC(R(C::*)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*&&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*&&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*&&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*&&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*&&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*&&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*&&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*&&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*&&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*&&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*const)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*const)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*const&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*const&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const&&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*const&&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*const&&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const&&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*volatile)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*volatile)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*volatile)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*volatile)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*volatile)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*volatile)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*volatile)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*volatile)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*volatile)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*volatile)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*volatile&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*volatile&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*volatile&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*volatile&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*volatile&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*volatile&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*volatile&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*volatile&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*volatile&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*volatile&&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*volatile&&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*volatile&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*volatile&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*volatile&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*volatile&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*volatile&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*volatile&&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*volatile&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*const volatile)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*const volatile)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const volatile)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const volatile)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const volatile)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const volatile)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const volatile)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const volatile)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const volatile)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*const volatile&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*const volatile&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const volatile&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const volatile&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const volatile&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const volatile&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const volatile&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const volatile&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...), R(C::*)(A...))
MEMBER_FUNC(R(C::*const volatile&&)(A...) noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) &, R(C::*)(A...))
MEMBER_FUNC(R(C::*const volatile&&)(A...) & noexcept, R(C::*)(A...) noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const &, R(C::*)(A...) const)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) volatile, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const volatile&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) volatile &, R(C::*)(A...) volatile)
MEMBER_FUNC(R(C::*const volatile&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const volatile, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const volatile &, R(C::*)(A...) const volatile)
MEMBER_FUNC(R(C::*const volatile&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*&&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*&&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*&&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*&&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*&&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*&&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*&&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*&&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*&&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*&&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*const)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*const)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*const&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*const&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const&&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*const&&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const&&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*const&&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const&&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const&&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const&&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const&&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const&&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const&&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*volatile)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*volatile)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*volatile)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*volatile)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*volatile)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*volatile)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*volatile)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*volatile)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*volatile)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*volatile)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*volatile)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*volatile)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*volatile)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*volatile)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*volatile)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*volatile)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*volatile&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*volatile&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*volatile&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*volatile&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*volatile&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*volatile&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*volatile&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*volatile&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*volatile&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*volatile&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*volatile&&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*volatile&&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*volatile&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*volatile&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*volatile&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*volatile&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*volatile&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*volatile&&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*volatile&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const volatile)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*const volatile)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*const volatile)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const volatile)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const volatile)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const volatile)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const volatile)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const volatile)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const volatile)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const volatile)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*const volatile&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*const volatile&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const volatile&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const volatile&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const volatile&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const volatile&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const volatile&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const volatile&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const volatile&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...), R(C::*)(A...))
STD_MEM_FN(R(C::*const volatile&&)(A...) noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) &, R(C::*)(A...))
STD_MEM_FN(R(C::*const volatile&&)(A...) & noexcept, R(C::*)(A...) noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) const, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const volatile&&)(A...) const noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) const &, R(C::*)(A...) const)
STD_MEM_FN(R(C::*const volatile&&)(A...) const & noexcept, R(C::*)(A...) const noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) volatile, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const volatile&&)(A...) volatile noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) volatile &, R(C::*)(A...) volatile)
STD_MEM_FN(R(C::*const volatile&&)(A...) volatile & noexcept, R(C::*)(A...) volatile noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) const volatile, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const volatile&&)(A...) const volatile noexcept, R(C::*)(A...) const volatile noexcept)
STD_MEM_FN(R(C::*const volatile&&)(A...) const volatile &, R(C::*)(A...) const volatile)
STD_MEM_FN(R(C::*const volatile&&)(A...) const volatile & noexcept, R(C::*)(A...) const volatile noexcept)


#undef NON_MEMBER_FUNC
#undef MEMBER_FUNC
#undef STD_MEM_FN


template <typename T, typename R, typename... A>
struct __isinstance__<T, Function<R(A...)>> : Returns<bool> {
    static constexpr bool operator()(const T& obj) {
        if (impl::cpp_like<T>) {
            return issubclass<T, Function<R(A...)>>();

        } else if constexpr (issubclass<T, Function<R(A...)>>()) {
            return ptr(obj) != nullptr;

        } else if constexpr (impl::is_object_exact<T>) {
            return ptr(obj) != nullptr && (
                PyFunction_Check(ptr(obj)) ||
                PyMethod_Check(ptr(obj)) ||
                PyCFunction_Check(ptr(obj))
            );
        } else {
            return false;
        }
    }
};


// TODO: if default specialization is given, type checks should be fully generic, right?
// issubclass<T, Function<>>() should check impl::is_callable_any<T>;

template <typename T, typename R, typename... A>
struct __issubclass__<T, Function<R(A...)>> : Returns<bool> {
    static constexpr bool operator()() {
        return std::is_invocable_r_v<R, T, A...>;
    }
    static constexpr bool operator()(const T&) {
        // TODO: this is going to have to be radically rethought.
        // Maybe I just forward to an issubclass() check against the type object?
        // In fact, this could maybe be standard operating procedure for all types.
        // 
        return PyType_IsSubtype(
            reinterpret_cast<PyTypeObject*>(ptr(Type<T>())),
            reinterpret_cast<PyTypeObject*>(ptr(Type<Function<R(A...)>>()))
        );
    }
};


template <typename Return, typename... Target, typename Func, typename... Values>
    requires (
        !impl::python_like<Func> &&
        std::is_invocable_r_v<Return, Func, Target...> &&
        Function<Return(Target...)>::Defaults::template enable<Values...>
    )
struct __init__<Function<Return(Target...)>, Func, Values...> {
    using type = Function<Return(Target...)>;
    static type operator()(Func&& func, Values&&... defaults) {
        return reinterpret_steal<type>(py::Type<type>::__python__::__create__(
            "",
            "",
            std::function(std::forward<Func>(func)),
            typename type::Defaults(std::forward<Values>(defaults)...)
        ));
    }
};


template <
    std::convertible_to<std::string> Name,
    typename Return,
    typename... Target,
    typename Func,
    typename... Values
>
    requires (
        !impl::python_like<Func> &&
        std::is_invocable_r_v<Return, Func, Target...> &&
        Function<Return(Target...)>::Defaults::template enable<Values...>
    )
struct __init__<Function<Return(Target...)>, Name, Func, Values...> {
    using type = Function<Return(Target...)>;
    static type operator()(Name&& name, Func&& func, Values&&... defaults) {
        return reinterpret_steal<type>(py::Type<type>::__python__::__create__(
            std::forward(name),
            "",
            std::function(std::forward<Func>(func)),
            typename type::Defaults(std::forward<Values>(defaults)...)
        ));
    }
};


template <
    std::convertible_to<std::string> Name,
    std::convertible_to<std::string> Doc,
    typename Return,
    typename... Target,
    typename Func,
    typename... Values
>
    requires (
        !impl::python_like<Func> &&
        std::is_invocable_r_v<Return, Func, Target...> &&
        Function<Return(Target...)>::Defaults::template enable<Values...>
    )
struct __init__<Function<Return(Target...)>, Name, Doc, Func, Values...> {
    using type = Function<Return(Target...)>;
    static type operator()(Name&& name, Doc&& doc, Func&& func, Values&&... defaults) {
        return reinterpret_steal<type>(py::Type<type>::__python__::__create__(
            std::forward(name),
            std::forward<Doc>(doc),
            std::function(std::forward<Func>(func)),
            typename type::Defaults(std::forward<Values>(defaults)...)
        ));
    }
};


/* Call the function with the given arguments.  If the wrapped function is of the
coupled Python type, then this will be translated into a raw C++ call, bypassing
Python entirely. */
template <impl::inherits<impl::FunctionTag> Self, typename... Args>
    requires (std::remove_reference_t<Self>::invocable<Args...>)
struct __call__<Self, Args...> : Returns<typename std::remove_reference_t<Self>::Return> {
    using Func = std::remove_reference_t<Self>;
    static decltype(auto) operator()(Self&& self, Args&&... args) {
        if constexpr (std::is_void_v<typename Func::Return>) {
            invoke(ptr(self), std::forward<Source>(args)...);
        } else {
            return invoke(ptr(self), std::forward<Source>(args)...);
        }
    }
};


/// TODO: __getitem__, __contains__, __iter__, __len__, __bool__






/* Get the name of the wrapped function. */
template <typename Signature> requires (impl::GetSignature<Signature>::enable)
[[nodiscard]] std::string Interface<Function<Signature>>::_get_name(this const auto& self) {
    // TODO: get the base type and check against it.
    if (true) {
        Type<Function> func_type;
        if (PyType_IsSubtype(
            Py_TYPE(ptr(self)),
            reinterpret_cast<PyTypeObject*>(ptr(func_type))
        )) {
            PyFunction* func = reinterpret_cast<PyFunction*>(ptr(self));
            return func->base.name;
        }
        // TODO: print out a detailed error message with both signatures
        throw TypeError("signature mismatch");
    }

    PyObject* result = PyObject_GetAttrString(ptr(self), "__name__");
    if (result == nullptr) {
        Exception::from_python();
    }
    Py_ssize_t length;
    const char* data = PyUnicode_AsUTF8AndSize(result, &length);
    Py_DECREF(result);
    if (data == nullptr) {
        Exception::from_python();
    }
    return std::string(data, length);
}


// // get_default<I>() returns a reference to the default value of the I-th argument
// // get_default<name>() returns a reference to the default value of the named argument

// // TODO:
// // .defaults -> MappingProxy<Str, Object>
// // .annotations -> MappingProxy<Str, Type<Object>>
// // .posonly -> Tuple<Str>
// // .kwonly -> Tuple<Str>

// // /* Get a read-only dictionary mapping argument names to their default values. */
// // __declspec(property(get=_get_defaults)) MappingProxy<Dict<Str, Object>> defaults;
// // [[nodiscard]] MappingProxy<Dict<Str, Object>> _get_defaults() const {
// //     // TODO: check for the PyFunction type and extract the defaults directly
// //     if (true) {
// //         Type<Function> func_type;
// //         if (PyType_IsSubtype(
// //             Py_TYPE(ptr(*this)),
// //             reinterpret_cast<PyTypeObject*>(ptr(func_type))
// //         )) {
// //             // TODO: generate a dictionary using a fold expression over the
// //             // defaults, then convert to a MappingProxy.
// //             // Or maybe return a std::unordered_map
// //             throw NotImplementedError();
// //         }
// //         // TODO: print out a detailed error message with both signatures
// //         throw TypeError("signature mismatch");
// //     }

// //     // check for positional defaults
// //     PyObject* pos_defaults = PyFunction_GetDefaults(ptr(*this));
// //     if (pos_defaults == nullptr) {
// //         if (code.kwonlyargcount() > 0) {
// //             Object kwdefaults = attr<"__kwdefaults__">();
// //             if (kwdefaults.is(None)) {
// //                 return MappingProxy(Dict<Str, Object>{});
// //             } else {
// //                 return MappingProxy(reinterpret_steal<Dict<Str, Object>>(
// //                     kwdefaults.release())
// //                 );
// //             }
// //         } else {
// //             return MappingProxy(Dict<Str, Object>{});
// //         }
// //     }

// //     // extract positional defaults
// //     size_t argcount = code.argcount();
// //     Tuple<Object> defaults = reinterpret_borrow<Tuple<Object>>(pos_defaults);
// //     Tuple<Str> names = code.varnames()[{argcount - defaults.size(), argcount}];
// //     Dict result;
// //     for (size_t i = 0; i < defaults.size(); ++i) {
// //         result[names[i]] = defaults[i];
// //     }

// //     // merge keyword-only defaults
// //     if (code.kwonlyargcount() > 0) {
// //         Object kwdefaults = attr<"__kwdefaults__">();
// //         if (!kwdefaults.is(None)) {
// //             result.update(Dict(kwdefaults));
// //         }
// //     }
// //     return result;
// // }

// // /* Set the default value for one or more arguments.  If nullopt is provided,
// // then all defaults will be cleared. */
// // void defaults(Dict&& dict) {
// //     Code code = this->code();

// //     // TODO: clean this up.  The logic should go as follows:
// //     // 1. check for positional defaults.  If found, build a dictionary with the new
// //     // values and remove them from the input dict.
// //     // 2. check for keyword-only defaults.  If found, build a dictionary with the
// //     // new values and remove them from the input dict.
// //     // 3. if any keys are left over, raise an error and do not update the signature
// //     // 4. set defaults to Tuple(positional_defaults.values()) and update kwdefaults
// //     // in-place.

// //     // account for positional defaults
// //     PyObject* pos_defaults = PyFunction_GetDefaults(self());
// //     if (pos_defaults != nullptr) {
// //         size_t argcount = code.argcount();
// //         Tuple<Object> defaults = reinterpret_borrow<Tuple<Object>>(pos_defaults);
// //         Tuple<Str> names = code.varnames()[{argcount - defaults.size(), argcount}];
// //         Dict positional_defaults;
// //         for (size_t i = 0; i < defaults.size(); ++i) {
// //             positional_defaults[*names[i]] = *defaults[i];
// //         }

// //         // merge new defaults with old ones
// //         for (const Object& key : positional_defaults) {
// //             if (dict.contains(key)) {
// //                 positional_defaults[key] = dict.pop(key);
// //             }
// //         }
// //     }

// //     // check for keyword-only defaults
// //     if (code.kwonlyargcount() > 0) {
// //         Object kwdefaults = attr<"__kwdefaults__">();
// //         if (!kwdefaults.is(None)) {
// //             Dict temp = {};
// //             for (const Object& key : kwdefaults) {
// //                 if (dict.contains(key)) {
// //                     temp[key] = dict.pop(key);
// //                 }
// //             }
// //             if (dict) {
// //                 throw ValueError("no match for arguments " + Str(List(dict.keys())));
// //             }
// //             kwdefaults |= temp;
// //         } else if (dict) {
// //             throw ValueError("no match for arguments " + Str(List(dict.keys())));
// //         }
// //     } else if (dict) {
// //         throw ValueError("no match for arguments " + Str(List(dict.keys())));
// //     }

// //     // TODO: set defaults to Tuple(positional_defaults.values()) and update kwdefaults

// // }

// // /* Get a read-only dictionary holding type annotations for the function. */
// // [[nodiscard]] MappingProxy<Dict<Str, Object>> annotations() const {
// //     PyObject* result = PyFunction_GetAnnotations(self());
// //     if (result == nullptr) {
// //         return MappingProxy(Dict<Str, Object>{});
// //     }
// //     return reinterpret_borrow<MappingProxy<Dict<Str, Object>>>(result);
// // }

// // /* Set the type annotations for the function.  If nullopt is provided, then the
// // current annotations will be cleared.  Otherwise, the values in the dictionary will
// // be used to update the current values in-place. */
// // void annotations(std::optional<Dict> annotations) {
// //     if (!annotations.has_value()) {  // clear all annotations
// //         if (PyFunction_SetAnnotations(self(), Py_None)) {
// //             Exception::from_python();
// //         }

// //     } else if (!annotations.value()) {  // do nothing
// //         return;

// //     } else {  // update annotations in-place
// //         Code code = this->code();
// //         Tuple<Str> args = code.varnames()[{0, code.argcount() + code.kwonlyargcount()}];
// //         MappingProxy existing = this->annotations();

// //         // build new dict
// //         Dict result = {};
// //         for (const Object& arg : args) {
// //             if (annotations.value().contains(arg)) {
// //                 result[arg] = annotations.value().pop(arg);
// //             } else if (existing.contains(arg)) {
// //                 result[arg] = existing[arg];
// //             }
// //         }

// //         // account for return annotation
// //         static const Str s_return = "return";
// //         if (annotations.value().contains(s_return)) {
// //             result[s_return] = annotations.value().pop(s_return);
// //         } else if (existing.contains(s_return)) {
// //             result[s_return] = existing[s_return];
// //         }

// //         // check for unmatched keys
// //         if (annotations.value()) {
// //             throw ValueError(
// //                 "no match for arguments " +
// //                 Str(List(annotations.value().keys()))
// //             );
// //         }

// //         // push changes
// //         if (PyFunction_SetAnnotations(self(), ptr(result))) {
// //             Exception::from_python();
// //         }
// //     }
// // }

// /* Set the default value for one or more arguments. */
// template <typename... Values> requires (sizeof...(Values) > 0)  // and all are valid
// void defaults(Values&&... values);

// /* Get a read-only mapping of argument names to their type annotations. */
// [[nodiscard]] MappingProxy<Dict<Str, Object>> annotations() const;

// /* Set the type annotation for one or more arguments. */
// template <typename... Annotations> requires (sizeof...(Annotations) > 0)  // and all are valid
// void annotations(Annotations&&... annotations);


namespace impl {

    /* A convenience function that calls a named method of a Python object using
    C++-style arguments.  Avoids the overhead of creating a temporary Function object. */
    template <StaticStr Name, typename Self, typename... Args>
        requires (
            __getattr__<std::decay_t<Self>, Name>::enable &&
            std::derived_from<typename __getattr__<std::decay_t<Self>, Name>::type, FunctionTag> &&
            __getattr__<std::decay_t<Self>, Name>::type::template invocable<Args...>
        )
    decltype(auto) call_method(Self&& self, Args&&... args) {
        using Func = __getattr__<std::decay_t<Self>, Name>::type;
        Object meth = reinterpret_steal<Object>(PyObject_GetAttr(
            ptr(self),
            TemplateString<Name>::ptr
        ));
        if (meth.is(nullptr)) {
            Exception::from_python();
        }
        try {
            return Func::template invoke<typename Func::ReturnType>(
                meth,
                std::forward<Args>(args)...
            );
        } catch (...) {
            throw;
        }
    }

    /* A convenience function that calls a named method of a Python type object using
    C++-style arguments.  Avoids the overhead of creating a temporary Function object. */
    template <typename Self, StaticStr Name, typename... Args>
        requires (
            __getattr__<std::decay_t<Self>, Name>::enable &&
            std::derived_from<typename __getattr__<std::decay_t<Self>, Name>::type, FunctionTag> &&
            __getattr__<std::decay_t<Self>, Name>::type::template invocable<Args...>
        )
    decltype(auto) call_static(Args&&... args) {
        using Func = __getattr__<std::decay_t<Self>, Name>::type;
        Object meth = reinterpret_steal<Object>(PyObject_GetAttr(
            ptr(Self::type),
            TemplateString<Name>::ptr
        ));
        if (meth.is(nullptr)) {
            Exception::from_python();
        }
        try {
            return Func::template invoke<typename Func::ReturnType>(
                meth,
                std::forward<Args>(args)...
            );
        } catch (...) {
            throw;
        }
    }

}


/////////////////////////
////    OPERATORS    ////
/////////////////////////


/// TODO: list all the special rules of container unpacking here?


/* A compile-time factory for binding keyword arguments with Python syntax.  constexpr
instances of this class can be used to provide an even more Pythonic syntax:

    constexpr auto x = py::arg<"x">;
    my_func(x = 42);
*/
template <StaticStr name>
constexpr impl::ArgFactory<name> arg {};


/* Dereference operator is used to emulate Python container unpacking. */
template <impl::python_like Self> requires (impl::iterable<Self>)
[[nodiscard]] auto operator*(Self&& self) -> impl::ArgPack<Self> {
    return {std::forward<Self>(self)};
}


/// TODO: Object::operator()


}  // namespace py


#endif
