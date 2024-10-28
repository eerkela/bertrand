#ifndef BERTRAND_PYTHON_CORE_FUNC_H
#define BERTRAND_PYTHON_CORE_FUNC_H

#include "declarations.h"
#include "object.h"
#include "except.h"
#include "ops.h"
#include "arg.h"
#include "iter.h"


namespace py {


namespace impl {

    /* Default seed for FNV-1a hash function. */
    constexpr size_t fnv1a_seed = [] {
        if constexpr (sizeof(size_t) > 4) {
            return 14695981039346656037ULL;
        } else {
            return 2166136261u;
        }
    }();

    /* Default prime for FNV-1a hash function. */
    constexpr size_t fnv1a_prime = [] {
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
                fnv1a_prime,
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
                fnv1a_prime,
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
    constexpr size_t fnv1a(const char* str, size_t seed, size_t prime) noexcept {
        while (*str) {
            seed ^= static_cast<size_t>(*str);
            seed *= prime;
            ++str;
        }
        return seed;
    }

    /* Round a number up to the next power of two unless it is one already. */
    template <std::unsigned_integral T>
    constexpr T next_power_of_two(T n) noexcept {
        constexpr size_t bits = sizeof(T) * 8;
        --n;
        for (size_t i = 1; i < bits; i <<= 1) {
            n |= (n >> i);
        }
        return ++n;
    }

    /* A simple, trivially-destructible representation of a single parameter in a
    function signature or call site. */
    struct Param {
        std::string_view name;
        PyObject* type;
        ArgKind kind;

        constexpr bool posonly() const noexcept { return kind.posonly(); }
        constexpr bool pos() const noexcept { return kind.pos(); }
        constexpr bool args() const noexcept { return kind.args(); }
        constexpr bool kwonly() const noexcept { return kind.kwonly(); }
        constexpr bool kw() const noexcept { return kind.kw(); }
        constexpr bool kwargs() const noexcept { return kind.kwargs(); }
        constexpr bool opt() const noexcept { return kind.opt(); }
        constexpr bool variadic() const noexcept { return kind.variadic(); }

        /* Compute a hash of this parameter's name, type, and kind, using the given
        FNV-1a hash seed and prime. */
        size_t hash(size_t seed, size_t prime) const noexcept {
            return hash_combine(
                fnv1a(name.data(), seed, prime),
                reinterpret_cast<size_t>(type),
                static_cast<size_t>(kind)
            );
        }

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
        size_t hash = 0;

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
            PyObject* typing = PyImport_Import(ptr(template_string<"typing">()));
            if (typing == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(typing);
        }

        static Object import_types() {
            PyObject* types = PyImport_Import(ptr(template_string<"types">()));
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
            PyObject* get_type_hints_args[] = {nullptr, ptr(func), Py_True};
            Object get_type_hints_kwnames = reinterpret_steal<Object>(
                PyTuple_Pack(1, ptr(template_string<"include_extras">()))
            );
            Object hints = reinterpret_steal<Object>(PyObject_Vectorcall(
                ptr(getattr<"get_type_hints">(typing)),
                get_type_hints_args + 1,
                1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                ptr(get_type_hints_kwnames)
            ));
            if (hints.is(nullptr)) {
                Exception::from_python();
            }
            Object empty = getattr<"empty">(getattr<"Parameter">(inspect));
            Object parameters = reinterpret_steal<Object>(PyObject_CallMethodNoArgs(
                ptr(getattr<"parameters">(signature)),
                ptr(template_string<"values">())
            ));
            Py_ssize_t len = PyObject_Length(ptr(parameters));
            if (len < 0) {
                Exception::from_python();
            }
            Object new_params = reinterpret_steal<Object>(
                PyList_New(len)
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
                    PyObject* replace_args[] = {nullptr, ptr(annotation)};
                    Object replace_kwnames = reinterpret_steal<Object>(
                        PyTuple_Pack(1, ptr(template_string<"annotation">()))
                    );
                    if (replace_kwnames.is(nullptr)) {
                        Exception::from_python();
                    }
                    param = reinterpret_steal<Object>(PyObject_Vectorcall(
                        ptr(getattr<"replace">(param)),
                        replace_args + 1,
                        0 | PY_VECTORCALL_ARGUMENTS_OFFSET,
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
                ptr(template_string<"return">())
            ));
            if (return_annotation.is(nullptr)) {
                return_annotation = empty;
            }
            PyObject* replace_args[] = {
                nullptr,
                ptr(return_annotation),
                ptr(new_params)
            };
            Object replace_kwnames = reinterpret_steal<Object>(PyTuple_Pack(
                2,
                ptr(template_string<"return_annotation">()),
                ptr(template_string<"parameters">())
            ));
            if (replace_kwnames.is(nullptr)) {
                Exception::from_python();
            }
            signature = reinterpret_steal<Object>(PyObject_Vectorcall(
                ptr(getattr<"replace">(signature)),
                replace_args + 1,
                0 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                ptr(replace_kwnames)
            ));
            if (signature.is(nullptr)) {
                Exception::from_python();
            }
            return signature;
        }

        Object get_parameters() const {
            Object values = reinterpret_steal<Object>(PyObject_CallMethodNoArgs(
                ptr(getattr<"parameters">(signature)),
                ptr(template_string<"values">())
            ));
            if (values.is(nullptr)) {
                Exception::from_python();
            }
            Object result = reinterpret_steal<Object>(
                PySequence_Tuple(ptr(values))
            );
            if (result.is(nullptr)) {
                Exception::from_python();
            }
            return result;
        }

        static Object to_union(std::set<PyObject*>& keys, const Object& Union) {
            Object key = reinterpret_steal<Object>(
                PyTuple_New(keys.size())
            );
            if (key.is(nullptr)) {
                Exception::from_python();
            }
            size_t i = 0;
            for (PyObject* type : keys) {
                PyTuple_SET_ITEM(ptr(key), i++, Py_NewRef(type));
            }
            Object specialization = reinterpret_steal<Object>(
                PyObject_GetItem(
                    ptr(Union),
                    ptr(key)
                )
            );
            if (specialization.is(nullptr)) {
                Exception::from_python();
            }
            return specialization;
        }

    public:
        Object bertrand = [] {
            PyObject* bertrand = PyImport_Import(
                ptr(template_string<"bertrand">())
            );
            if (bertrand == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(bertrand);
        }();
        Object inspect = [] {
            PyObject* inspect = PyImport_Import(ptr(template_string<"inspect">()));
            if (inspect == nullptr) {
                Exception::from_python();
            }
            return reinterpret_steal<Object>(inspect);
        }();
        Object typing = import_typing();

        Object func;
        Object signature;
        Object parameters;
        size_t seed;
        size_t prime;

        Inspect(
            const Object& func,
            size_t seed,
            size_t prime
        ) : func(func),
            signature(get_signature()),
            parameters(get_parameters()),
            seed(seed),
            prime(prime)
        {}

        Inspect(
            Object&& func,
            size_t seed,
            size_t prime
        ) : func(std::move(func)),
            signature(get_signature()),
            parameters(get_parameters()),
            seed(seed),
            prime(prime)
        {}

        Inspect(const Inspect& other) = delete;
        Inspect(Inspect&& other) = delete;
        Inspect& operator=(const Inspect& other) = delete;
        Inspect& operator=(Inspect&& other) noexcept = delete;

        /* Get the `inspect.Parameter` object at a particular index of the introspected
        function signature. */
        Object at(size_t i) const {
            Py_ssize_t len = PyObject_Length(ptr(parameters));
            if (len < 0) {
                Exception::from_python();
            } else if (i >= len) {
                throw IndexError("index out of range");
            }
            return reinterpret_borrow<Object>(
                PyTuple_GET_ITEM(ptr(parameters), i)
            );
        }

        /* A callback function to use when parsing inline type hints within a Python
        function declaration. */
        struct Callback {
            std::string id;
            std::function<bool(Object, std::set<PyObject*>&)> func;
            bool operator()(Object hint, std::set<PyObject*>& out) const {
                return func(hint, out);
            }
        };

        /* Initiate a search of the callback map in order to parse a Python-style type
        hint.  The search stops at the first callback that returns true, otherwise the
        hint is interpreted as either a single type if it is a Python class, or a
        generic `object` type otherwise. */
        static void parse(Object hint, std::set<PyObject*>& out) {
            for (const Callback& cb : callbacks) {
                if (cb(hint, out)) {
                    return;
                }
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

            // unrecognized hints are assumed to implement `issubclass()`
            out.insert(ptr(hint));
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

        For performance reasons, the types that are added to the `out` set are
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
                [](Object hint, std::set<PyObject*>& out) -> bool {
                    Object types = import_types();
                    int rc = PyObject_IsInstance(
                        ptr(hint),
                        ptr(getattr<"GenericAlias">(types))
                    );
                    if (rc < 0) {
                        Exception::from_python();
                    } else if (rc) {
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
                [](Object hint, std::set<PyObject*>& out) -> bool {
                    Object types = import_types();
                    int rc = PyObject_IsInstance(
                        ptr(hint),
                        ptr(getattr<"UnionType">(types))
                    );
                    if (rc < 0) {
                        Exception::from_python();
                    } else if (rc) {
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
                [](Object hint, std::set<PyObject*>& out) -> bool {
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
                [](Object hint, std::set<PyObject*>& out) -> bool {
                    Object typing = import_typing();
                    Object origin = reinterpret_steal<Object>(PyObject_CallOneArg(
                        ptr(getattr<"get_origin">(typing)),
                        ptr(hint)
                    ));
                    if (origin.is(nullptr)) {
                        Exception::from_python();
                    } else if (origin.is(getattr<"Any">(typing))) {
                        out.insert(reinterpret_cast<PyObject*>(&PyBaseObject_Type));
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.TypeAliasType",
                [](Object hint, std::set<PyObject*>& out) -> bool {
                    Object typing = import_typing();
                    int rc = PyObject_IsInstance(
                        ptr(hint),
                        ptr(getattr<"TypeAliasType">(typing))
                    );
                    if (rc < 0) {
                        Exception::from_python();
                    } else if (rc) {
                        parse(getattr<"__value__">(hint), out);
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.Literal",
                [](Object hint, std::set<PyObject*>& out) -> bool {
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
                            out.insert(reinterpret_cast<PyObject*>(
                                Py_TYPE(PyTuple_GET_ITEM(ptr(args), i))
                            ));
                        }
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.LiteralString",
                [](Object hint, std::set<PyObject*>& out) -> bool {
                    Object typing = import_typing();
                    if (hint.is(getattr<"LiteralString">(typing))) {
                        out.insert(reinterpret_cast<PyObject*>(&PyUnicode_Type));
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.AnyStr",
                [](Object hint, std::set<PyObject*>& out) -> bool {
                    Object typing = import_typing();
                    if (hint.is(getattr<"AnyStr">(typing))) {
                        out.insert(reinterpret_cast<PyObject*>(&PyUnicode_Type));
                        out.insert(reinterpret_cast<PyObject*>(&PyBytes_Type));
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.NoReturn",
                [](Object hint, std::set<PyObject*>& out) -> bool {
                    Object typing = import_typing();
                    if (
                        hint.is(getattr<"NoReturn">(typing)) ||
                        hint.is(getattr<"Never">(typing))
                    ) {
                        /// NOTE: this handler models NoReturn/Never by not pushing a
                        /// type to the `out` set, giving an empty return type.
                        return true;
                    }
                    return false;
                }
            },
            {
                "typing.TypeGuard",
                [](Object hint, std::set<PyObject*>& out) -> bool {
                    Object typing = import_typing();
                    Object origin = reinterpret_steal<Object>(PyObject_CallOneArg(
                        ptr(getattr<"get_origin">(typing)),
                        ptr(hint)
                    ));
                    if (origin.is(nullptr)) {
                        Exception::from_python();
                    } else if (origin.is(getattr<"TypeGuard">(typing))) {
                        out.insert(reinterpret_cast<PyObject*>(&PyBool_Type));
                        return true;
                    }
                    return false;
                }
            }
        };

        /* Get the possible return type of the function, using the same callback
        handlers as the parameters.  Note that functions with `typing.NoReturn` or
        `typing.Never` annotations can return a null pointer.  Otherwise, returns
        the parsed return type of the function, which may be a specialization of
        `Union` if multiple return types are valid. */
        PyObject* returns() const {
            if (return_initialized) {
                return _returns;
            }
            Object return_annotation = getattr<"return_annotation">(signature);
            std::set<PyObject*> keys;
            if (return_annotation.is(getattr<"empty">(signature))) {
                keys.insert(reinterpret_cast<PyObject*>(&PyBaseObject_Type));
            } else {
                parse(return_annotation, keys);
            }
            if (keys.empty()) {
                _returns = nullptr;
            } else if (keys.size() == 1) {
                _returns = *keys.begin();
            } else {
                _returns = ptr(to_union(
                    keys,
                    getattr<"Union">(bertrand)
                ));
            }
            return_initialized = true;
            return _returns;
        }

        /* Convert the introspected signature into a lightweight C++ template key,
        suitable for insertion into a function's overload trie. */
        const Params<std::vector<Param>>& key() const {
            if (key_initialized) {
                return _key;
            }

            Object Parameter = getattr<"Parameter">(inspect);
            Object empty = getattr<"empty">(Parameter);
            Object POSITIONAL_ONLY = getattr<"POSITIONAL_ONLY">(Parameter);
            Object POSITIONAL_OR_KEYWORD = getattr<"POSITIONAL_OR_KEYWORD">(Parameter);
            Object VAR_POSITIONAL = getattr<"VAR_POSITIONAL">(Parameter);
            Object KEYWORD_ONLY = getattr<"KEYWORD_ONLY">(Parameter);
            Object VAR_KEYWORD = getattr<"VAR_KEYWORD">(Parameter);

            Py_ssize_t len = PyObject_Length(ptr(parameters));
            if (len < 0) {
                Exception::from_python();
            }
            _key.value.reserve(len);
            for (Object param : parameters) {
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

                std::set<PyObject*> types;
                if (getattr<"annotation">(param).is(empty)) {
                    types.insert(reinterpret_cast<PyObject*>(&PyBaseObject_Type));
                } else {
                    parse(param, types);
                }
                if (types.empty()) {
                    throw TypeError(
                        "invalid type hint for parameter '" + std::string(name) +
                        "': " + repr(getattr<"annotation">(param))
                    );
                } else if (types.size() == 1) {
                    PyObject* type = *types.begin();
                    _key.value.push_back({
                        name,
                        type,
                        category
                    });
                    _key.hash = hash_combine(
                        _key.hash,
                        fnv1a(name.data(), seed, prime),
                        reinterpret_cast<size_t>(type)
                    );
                } else {
                    Object specialization = to_union(
                        types,
                        getattr<"Union">(bertrand)
                    );
                    _key.value.push_back({
                        name,
                        ptr(specialization),
                        category
                    });
                    _key.hash = hash_combine(
                        _key.hash,
                        fnv1a(name.data(), seed, prime),
                        reinterpret_cast<size_t>(ptr(specialization))
                    );
                }
            }
            key_initialized = true;
            return _key;
        }

        /* Convert the inspected signature into a valid template key for the
        `bertrand.Function` class on the Python side. */
        Object template_key() const {
            if (!_template_key.is(nullptr)) {
                return _template_key;
            }

            Object Parameter = getattr<"Parameter">(inspect);
            Object empty = getattr<"empty">(Parameter);
            Object POSITIONAL_ONLY = getattr<"POSITIONAL_ONLY">(Parameter);
            Object POSITIONAL_OR_KEYWORD = getattr<"POSITIONAL_OR_KEYWORD">(Parameter);
            Object VAR_POSITIONAL = getattr<"VAR_POSITIONAL">(Parameter);
            Object KEYWORD_ONLY = getattr<"KEYWORD_ONLY">(Parameter);

            Py_ssize_t len = PyObject_Length(ptr(parameters));
            if (len < 0) {
                Exception::from_python();
            }
            Object result = reinterpret_steal<Object>(
                PyTuple_New(len + 1)
            );
            if (result.is(nullptr)) {
                Exception::from_python();
            }

            // first element lists type of bound `self` argument, descriptor type, and
            // return type as a slice
            PyObject* returns = this->returns();
            if (!returns || returns == reinterpret_cast<PyObject*>(Py_TYPE(Py_None))) {
                returns = Py_None;
            }
            Object self = getattr<"__self__">(func, None);
            if (PyType_Check(ptr(self))) {
                PyObject* slice = PySlice_New(
                    ptr(self),
                    reinterpret_cast<PyObject*>(&PyClassMethodDescr_Type),
                    returns
                );
                if (slice == nullptr) {
                    Exception::from_python();
                }
                PyTuple_SET_ITEM(ptr(result), 0, slice);  // steals a reference
            } else {
                PyObject* slice = PySlice_New(
                    reinterpret_cast<PyObject*>(Py_TYPE(ptr(self))),
                    Py_None,
                    returns
                );
                if (slice == nullptr) {
                    Exception::from_python();
                }
                PyTuple_SET_ITEM(ptr(result), 0, slice);  // steals a reference
            }

            /// remaining elements are parameters, with slices, '/', '*', etc.
            const Params<std::vector<Param>>& key = this->key();
            Py_ssize_t offset = 1;
            Py_ssize_t posonly_idx = std::numeric_limits<Py_ssize_t>::max();
            Py_ssize_t kwonly_idx = std::numeric_limits<Py_ssize_t>::max();
            for (Py_ssize_t i = 0; i < len; ++i) {
                const Param& param = key[i];
                if (param.posonly()) {
                    posonly_idx = i;
                    if (!param.opt()) {
                        PyTuple_SET_ITEM(
                            ptr(result),
                            i + offset,
                            Py_NewRef(param.type)
                        );
                    } else {
                        PyObject* slice = PySlice_New(
                            param.type,
                            Py_Ellipsis,
                            Py_None
                        );
                        if (slice == nullptr) {
                            Exception::from_python();
                        }
                        PyTuple_SET_ITEM(ptr(result), i + offset, slice);
                    }
                } else {
                    // insert '/' delimiter if there are any posonly arguments
                    if (i > posonly_idx) {
                        PyObject* grow;
                        if (_PyTuple_Resize(&grow, len + offset + 1) < 0) {
                            Exception::from_python();
                        }
                        result = reinterpret_steal<Object>(grow);
                        PyTuple_SET_ITEM(
                            ptr(result),
                            i + offset,
                            release(template_string<"/">())
                        );
                        ++offset;

                    // insert '*' delimiter if there are any kwonly arguments
                    } else if (
                        param.kwonly() &&
                        kwonly_idx == std::numeric_limits<Py_ssize_t>::max()
                    ) {
                        kwonly_idx = i;
                        PyObject* grow;
                        if (_PyTuple_Resize(&grow, len + offset + 1) < 0) {
                            Exception::from_python();
                        }
                        result = reinterpret_steal<Object>(grow);
                        PyTuple_SET_ITEM(
                            ptr(result),
                            i + offset,
                            release(template_string<"*">())
                        );
                        ++offset;
                    }

                    // insert parameter identifier
                    Object name = reinterpret_steal<Object>(
                        PyUnicode_FromStringAndSize(
                            param.name.data(),
                            param.name.size()
                        )
                    );
                    if (name.is(nullptr)) {
                        Exception::from_python();
                    }
                    PyObject* slice = PySlice_New(
                        ptr(name),
                        param.type,
                        param.opt() ? Py_Ellipsis : Py_None
                    );
                    if (slice == nullptr) {
                        Exception::from_python();
                    }
                    PyTuple_SET_ITEM(ptr(result), i + offset, slice);
                }
            }
            return result;
        }

    private:
        mutable bool return_initialized = false;
        mutable PyObject* _returns;
        mutable bool key_initialized = false;
        mutable Params<std::vector<Param>> _key = {std::vector<Param>{}, 0};
        mutable Object _template_key;
    };

    /* Inspect an annotated C++ parameter list at compile time and extract metadata
    that allows a corresponding function to be called with Python-style arguments from
    C++. */
    template <typename... Args>
    struct Arguments : BertrandTag {
    private:

        template <typename... Ts>
        static constexpr size_t _n_posonly = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_posonly<T, Ts...> =
            _n_posonly<Ts...> + ArgTraits<T>::posonly();

        template <typename... Ts>
        static constexpr size_t _n_pos = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_pos<T, Ts...> =
            _n_pos<Ts...> + ArgTraits<T>::pos();

        template <typename... Ts>
        static constexpr size_t _n_kw = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_kw<T, Ts...> =
            _n_kw<Ts...> + ArgTraits<T>::kw();

        template <typename... Ts>
        static constexpr size_t _n_kwonly = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_kwonly<T, Ts...> =
            _n_kwonly<Ts...> + ArgTraits<T>::kwonly();

        template <typename... Ts>
        static constexpr size_t _n_opt = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_opt<T, Ts...> =
            _n_opt<Ts...> + ArgTraits<T>::opt();

        template <typename... Ts>
        static constexpr size_t _n_opt_posonly = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_opt_posonly<T, Ts...> =
            _n_opt_posonly<Ts...> + (ArgTraits<T>::posonly() && ArgTraits<T>::opt());

        template <typename... Ts>
        static constexpr size_t _n_opt_pos = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_opt_pos<T, Ts...> =
            _n_opt_pos<Ts...> + (ArgTraits<T>::pos() && ArgTraits<T>::opt());

        template <typename... Ts>
        static constexpr size_t _n_opt_kw = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_opt_kw<T, Ts...> =
            _n_opt_kw<Ts...> + (ArgTraits<T>::kw() && ArgTraits<T>::opt());

        template <typename... Ts>
        static constexpr size_t _n_opt_kwonly = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _n_opt_kwonly<T, Ts...> =
            _n_opt_kwonly<Ts...> + (ArgTraits<T>::kwonly() && ArgTraits<T>::opt());

        template <StaticStr Name, typename... Ts>
        static constexpr size_t _idx = 0;
        template <StaticStr Name, typename T, typename... Ts>
        static constexpr size_t _idx<Name, T, Ts...> =
            ArgTraits<T>::name == Name ? 0 : _idx<Name, Ts...> + 1;

        template <typename... Ts>
        static constexpr size_t _args_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _args_idx<T, Ts...> =
            ArgTraits<T>::args() ? 0 : _args_idx<Ts...> + 1;

        template <typename... Ts>
        static constexpr size_t _kw_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _kw_idx<T, Ts...> =
            ArgTraits<T>::kw() ? 0 : _kw_idx<Ts...> + 1;

        template <typename... Ts>
        static constexpr size_t _kwonly_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _kwonly_idx<T, Ts...> =
            ArgTraits<T>::kwonly() ? 0 : _kwonly_idx<Ts...> + 1;

        template <typename... Ts>
        static constexpr size_t _kwargs_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _kwargs_idx<T, Ts...> =
            ArgTraits<T>::kwargs() ? 0 : _kwargs_idx<Ts...> + 1;

        template <typename... Ts>
        static constexpr size_t _opt_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _opt_idx<T, Ts...> =
            ArgTraits<T>::opt() ? 0 : _opt_idx<Ts...> + 1;

        template <typename... Ts>
        static constexpr size_t _opt_posonly_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _opt_posonly_idx<T, Ts...> =
            ArgTraits<T>::posonly() && ArgTraits<T>::opt() ?
                0 : _opt_posonly_idx<Ts...> + 1;

        template <typename... Ts>
        static constexpr size_t _opt_pos_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _opt_pos_idx<T, Ts...> =
            ArgTraits<T>::pos() && ArgTraits<T>::opt() ?
                0 : _opt_pos_idx<Ts...> + 1;

        template <typename... Ts>
        static constexpr size_t _opt_kw_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _opt_kw_idx<T, Ts...> =
            ArgTraits<T>::kw() && ArgTraits<T>::opt() ?
                0 : _opt_kw_idx<Ts...> + 1;

        template <typename... Ts>
        static constexpr size_t _opt_kwonly_idx = 0;
        template <typename T, typename... Ts>
        static constexpr size_t _opt_kwonly_idx<T, Ts...> =
            ArgTraits<T>::kwonly() && ArgTraits<T>::opt() ?
                0 : _opt_kwonly_idx<Ts...> + 1;

    public:
        static constexpr size_t n                   = sizeof...(Args);
        static constexpr size_t n_posonly           = _n_posonly<Args...>;
        static constexpr size_t n_pos               = _n_pos<Args...>;
        static constexpr size_t n_kw                = _n_kw<Args...>;
        static constexpr size_t n_kwonly            = _n_kwonly<Args...>;
        static constexpr size_t n_opt               = _n_opt<Args...>;
        static constexpr size_t n_opt_posonly       = _n_opt_posonly<Args...>;
        static constexpr size_t n_opt_pos           = _n_opt_pos<Args...>;
        static constexpr size_t n_opt_kw            = _n_opt_kw<Args...>;
        static constexpr size_t n_opt_kwonly        = _n_opt_kwonly<Args...>;

        template <StaticStr Name>
        static constexpr bool has                   = _idx<Name, Args...> != n;
        static constexpr bool has_posonly           = n_posonly > 0;
        static constexpr bool has_pos               = n_pos > 0;
        static constexpr bool has_kw                = n_kw > 0;
        static constexpr bool has_kwonly            = n_kwonly > 0;
        static constexpr bool has_opt               = n_opt > 0;
        static constexpr bool has_opt_posonly       = n_opt_posonly > 0;
        static constexpr bool has_opt_pos           = n_opt_pos > 0;
        static constexpr bool has_opt_kw            = n_opt_kw > 0;
        static constexpr bool has_opt_kwonly        = n_opt_kwonly > 0;
        static constexpr bool has_args              = _args_idx<Args...> != n;
        static constexpr bool has_kwargs            = _kwargs_idx<Args...> != n;

        template <StaticStr Name> requires (has<Name>)
        static constexpr size_t idx                 = _idx<Name, Args...>;
        static constexpr size_t kw_idx              = _kw_idx<Args...>;
        static constexpr size_t kwonly_idx          = _kwonly_idx<Args...>;
        static constexpr size_t opt_idx             = _opt_idx<Args...>;
        static constexpr size_t opt_posonly_idx     = _opt_posonly_idx<Args...>;
        static constexpr size_t opt_pos_idx         = _opt_pos_idx<Args...>;
        static constexpr size_t opt_kw_idx          = _opt_kw_idx<Args...>;
        static constexpr size_t opt_kwonly_idx      = _opt_kwonly_idx<Args...>;
        static constexpr size_t args_idx            = _args_idx<Args...>;
        static constexpr size_t kwargs_idx          = _kwargs_idx<Args...>;

        template <size_t I> requires (I < n)
        using at = unpack_type<I, Args...>;

    private:

        template <size_t I, typename... Ts>
        static constexpr bool _proper_argument_order = true;
        template <size_t I, typename T, typename... Ts>
        static constexpr bool _proper_argument_order<I, T, Ts...> =
            (ArgTraits<T>::posonly() && (I > kw_idx || I > args_idx || I > kwargs_idx)) ||
            (ArgTraits<T>::pos() && (I > args_idx || I > kwonly_idx || I > kwargs_idx)) ||
            (ArgTraits<T>::args() && (I > kwonly_idx || I > kwargs_idx)) ||
            (ArgTraits<T>::kwonly() && (I > kwargs_idx)) ?
                false : _proper_argument_order<I + 1, Ts...>;

        template <size_t I, typename... Ts>
        static constexpr bool _no_duplicate_arguments = true;
        template <size_t I, typename T, typename... Ts>
        static constexpr bool _no_duplicate_arguments<I, T, Ts...> =
            (T::name != "" && I != idx<ArgTraits<T>::name>) ||
            (ArgTraits<T>::args() && I != args_idx) ||
            (ArgTraits<T>::kwargs() && I != kwargs_idx) ?
                false : _no_duplicate_arguments<I + 1, Ts...>;

        template <size_t I, typename... Ts>
        static constexpr bool _no_required_after_default = true;
        template <size_t I, typename T, typename... Ts>
        static constexpr bool _no_required_after_default<I, T, Ts...> =
            ArgTraits<T>::pos() && !ArgTraits<T>::opt() && I > opt_idx ?
                false : _no_required_after_default<I + 1, Ts...>;

        template <size_t I, typename... Ts>
        static constexpr bool _compatible() {
            return
                I == n ||
                (I == args_idx && args_idx == n - 1) ||
                (I == kwargs_idx && kwargs_idx == n - 1);
        };
        template <size_t I, typename T, typename... Ts>
        static constexpr bool _compatible() {
            if constexpr (ArgTraits<at<I>>::posonly()) {
                return
                    ArgTraits<T>::posonly() &&
                    !(ArgTraits<at<I>>::opt() && !ArgTraits<T>::opt()) &&
                    (ArgTraits<at<I>>::name == ArgTraits<T>::name) &&
                    issubclass<
                        typename ArgTraits<T>::type,
                        typename ArgTraits<at<I>>::type
                    >() &&
                    _compatible<I + 1, Ts...>();

            } else if constexpr (ArgTraits<at<I>>::pos()) {
                return
                    (ArgTraits<T>::pos() && ArgTraits<T>::kw()) &&
                    !(ArgTraits<at<I>>::opt() && !ArgTraits<T>::opt()) &&
                    (ArgTraits<at<I>>::name == ArgTraits<T>::name) &&
                    issubclass<
                        typename ArgTraits<T>::type,
                        typename ArgTraits<at<I>>::type
                    >() &&
                    _compatible<I + 1, Ts...>();

            } else if constexpr (ArgTraits<at<I>>::kw()) {
                return
                    (ArgTraits<T>::kw() && ArgTraits<T>::pos()) &&
                    !(ArgTraits<at<I>>::opt() && !ArgTraits<T>::opt()) &&
                    (ArgTraits<at<I>>::name == ArgTraits<T>::name) &&
                    issubclass<
                        typename ArgTraits<T>::type,
                        typename ArgTraits<at<I>>::type
                    >() &&
                    _compatible<I + 1, Ts...>();

            } else if constexpr (ArgTraits<at<I>>::args()) {
                if constexpr (ArgTraits<T>::pos() || ArgTraits<T>::args()) {
                    if constexpr (!issubclass<
                        typename ArgTraits<T>::type,
                        typename ArgTraits<at<I>>::type
                    >()) {
                        return false;
                    }
                    return _compatible<I, Ts...>();
                }
                return _compatible<I + 1, Ts...>();

            } else if constexpr (ArgTraits<at<I>>::kwargs()) {
                if constexpr (ArgTraits<T>::kw() || ArgTraits<T>::kwargs()) {
                    if constexpr (!issubclass<
                        typename ArgTraits<T>::type,
                        typename ArgTraits<at<I>>::type
                    >()) {
                        return false;
                    }
                    return _compatible<I, Ts...>();
                }
                return _compatible<I + 1, Ts...>();

            } else {
                static_assert(false, "unrecognized parameter type");
                return false;
            }
        }

        template <size_t I>
        static constexpr uint64_t _required =
            ArgTraits<unpack_type<I, Args...>>::opt() ||
            ArgTraits<unpack_type<I, Args...>>::variadic() ?
                0ULL : 1ULL << I;

    public:

        static constexpr bool args_are_convertible_to_python =
            (std::convertible_to<typename ArgTraits<Args>::type, Object> && ...);
        static constexpr bool proper_argument_order =
            _proper_argument_order<0, Args...>;
        static constexpr bool no_duplicate_arguments =
            _no_duplicate_arguments<0, Args...>;
        static constexpr bool no_required_after_default =
            _no_required_after_default<0, Args...>;

        /* A template constraint that evaluates true if another signature represents a
        viable overload of a function with this signature. */
        template <typename... Ts>
        static constexpr bool compatible = _compatible<0, Ts...>(); 

        /* A single entry in a callback table, storing the argument name, a one-hot
        encoded bitmask specifying this argument's position, a function that can be
        used to validate the argument, and a lazy function that can be used to retrieve
        its corresponding Python type. */
        struct Callback {
            std::string_view name;
            uint64_t mask = 0;
            bool(*func)(PyObject*) = nullptr;
            PyObject*(*type)() = nullptr;
            explicit operator bool() const noexcept { return func != nullptr; }
            bool operator()(PyObject* type) const { return func(type); }
        };

        /* A bitmask with a 1 in the position of all of the required arguments in the
        parameter list.

        Each callback stores a one-hot encoded mask that is joined into a single
        bitmask as each argument is processed.  The resulting mask can then be compared
        to this constant to determine if all required arguments have been provided.  If
        that comparison evaluates to false, then further bitwise inspection can be done
        to determine exactly which arguments were missing, as well as their names.

        Note that this mask effectively limits the number of arguments that a function
        can accept to 64, which is a reasonable limit for most functions.  The
        performance benefits justify the limitation, and if you need more than 64
        arguments, you should probably be using a different design pattern. */
        static constexpr uint64_t required = []<size_t... Is>(std::index_sequence<Is...>) {
            return (_required<Is> | ...);
        }(std::make_index_sequence<n>{});

    private:
        static constexpr Callback null_callback;

        /* Populate the positional argument table with an appropriate callback for
        each argument in the parameter list. */
        template <size_t I>
        static consteval Callback populate_positional_table() {
            using T = at<I>;
            return {
                ArgTraits<T>::name,
                ArgTraits<T>::variadic() ? 0ULL : 1ULL << I,
                [](PyObject* type) -> bool {
                    using U = ArgTraits<T>::type;
                    if constexpr (has_type<U>) {
                        int rc = PyObject_IsSubclass(
                            type,
                            ptr(Type<U>())
                        );
                        if (rc < 0) {
                            Exception::from_python();
                        }
                        return rc;
                    } else {
                        throw TypeError(
                            "C++ type has no Python equivalent: " +
                            demangle(typeid(U).name())
                        );
                    }
                },
                []() -> PyObject* {
                    using U = ArgTraits<T>::type;
                    if constexpr (has_type<U>) {
                        return ptr(Type<U>());
                    } else {
                        throw TypeError(
                            "C++ type has no Python equivalent: " +
                            demangle(typeid(U).name())
                        );
                    }
                }
            };
        }

        /* An array of positional arguments to callbacks and bitmasks that can be used
        to validate the argument dynamically at runtime. */
        static constexpr auto positional_table = []<size_t... Is>(
            std::index_sequence<Is...>
        ) -> std::array<Callback, n> {
            return {populate_positional_table<Is>()...};
        }(std::make_index_sequence<n>{});

        /* Get the size of the perfect hash table required to efficiently validate
        keyword arguments as a power of 2. */
        static constexpr size_t keyword_table_size = next_power_of_two(2 * n_kw);

        /* Take the modulus with respect to the keyword table by exploiting the power
        of 2 table size. */
        static constexpr size_t keyword_modulus(size_t hash) {
            return hash & (keyword_table_size - 1);
        }

        /* Given a precomputed keyword index into the hash table, check to see if any
        subsequent arguments in the parameter list hash to the same index. */
        template <typename...>
        static consteval bool _collisions(size_t, size_t, size_t) { return false; }
        template <typename T, typename... Ts>
        static consteval bool _collisions(size_t idx, size_t seed, size_t prime) {
            if constexpr (ArgTraits<T>::kw) {
                size_t hash = fnv1a(
                    ArgTraits<T>::name,
                    seed,
                    prime
                );
                return
                    (keyword_modulus(hash) == idx) ||
                    _collisions<Ts...>(idx, seed, prime);
            } else {
                return _collisions<Ts...>(idx, seed, prime);
            }
        }

        /* Check to see if the candidate seed and prime produce any collisions for the
        observed keyword arguments. */
        template <typename...>
        static consteval bool collisions(size_t seed, size_t prime) { return false; }
        template <typename T, typename... Ts>
        static consteval bool collisions(size_t seed, size_t prime) {
            if constexpr (ArgTraits<T>::kw) {
                size_t hash = fnv1a(
                    ArgTraits<T>::name,
                    seed,
                    prime
                );
                return _collisions<Ts...>(
                    keyword_modulus(hash),
                    seed,
                    prime
                ) || collisions<Ts...>(seed, prime);
            } else {
                return collisions<Ts...>(seed, prime);
            }
        }

        /* Find an FNV-1a seed and prime that produces perfect hashes with respect to
        the keyword table size. */
        static constexpr auto hash_components = [] -> std::pair<size_t, size_t> {
            constexpr size_t recursion_limit = fnv1a_seed + 100'000;
            size_t seed = fnv1a_seed;
            size_t prime = fnv1a_prime;
            size_t i = 0;
            while (collisions<Args...>(seed, prime)) {
                if (++seed > recursion_limit) {
                    if (++i == 10) {
                        std::cerr << "error: unable to find a perfect hash seed "
                                  << "after 10^6 iterations.  Consider increasing the "
                                  << "recursion limit or reviewing the keyword "
                                  << "argument names for potential issues.\n";
                        std::exit(1);
                    }
                    seed = fnv1a_seed;
                    prime = fnv1a_fallback_primes[i];
                }
            }
            return {seed, prime};
        }();

        /* Populate the keyword table with an appropriate callback for each keyword
        argument in the parameter list. */
        template <size_t I>
        static consteval void populate_keyword_table(
            std::array<Callback, keyword_table_size>& table,
            size_t seed,
            size_t prime
        ) {
            using T = at<I>;
            if constexpr (ArgTraits<T>::kw()) {
                constexpr size_t i = keyword_modulus(hash(ArgTraits<T>::name));
                table[i] = {
                    ArgTraits<T>::name,
                    ArgTraits<T>::variadic() ? 0ULL : 1ULL << I,
                    [](PyObject* type) -> bool {
                        using U = ArgTraits<T>::type;
                        if constexpr (has_type<U>) {
                            int rc = PyObject_IsSubclass(
                                type,
                                ptr(Type<U>())
                            );
                            if (rc < 0) {
                                Exception::from_python();
                            }
                            return rc;
                        } else {
                            throw TypeError(
                                "C++ type has no Python equivalent: " +
                                demangle(typeid(U).name())
                            );
                        }
                    },
                    []() -> PyObject* {
                        using U = ArgTraits<T>::type;
                        if constexpr (has_type<U>) {
                            return ptr(Type<U>());
                        } else {
                            throw TypeError(
                                "C++ type has no Python equivalent: " +
                                demangle(typeid(U).name())
                            );
                        }
                    }
                };
            }
        }

        /* The keyword table itself.  Each entry holds the expected keyword name for
        validation as well as a callback that can be used to validate its type
        dynamically at runtime. */
        static constexpr auto keyword_table = []<size_t... Is>(
            std::index_sequence<Is...>,
            size_t seed,
            size_t prime
        ) {
            std::array<Callback, keyword_table_size> table;
            (populate_keyword_table<Is>(table, seed, prime), ...);
            return table;
        }(
            std::make_index_sequence<n>{},
            hash_components.first,
            hash_components.second
        );

        template <size_t I>
        static Param _parameters(size_t& hash) {
            using T = at<I>;
            constexpr Callback& callback = positional_table[I];
            PyObject* type = callback.type();
            hash = hash_combine(
                hash,
                fnv1a(
                    ArgTraits<T>::name,
                    hash_components.first,
                    hash_components.second
                ),
                reinterpret_cast<size_t>(type)
            );
            return {ArgTraits<T>::name, type, ArgTraits<T>::kind};
        }

    public:

        /* A seed for an FNV-1a hash algorithm that was found to perfectly hash the
        keyword argument names from the enclosing parameter list. */
        static constexpr size_t seed = hash_components.first;

        /* A prime for an FNV-1a hash algorithm that was found to perfectly hash the
        keyword argument names from the enclosing parameter list. */
        static constexpr size_t prime = hash_components.second;

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
                return i < args_idx ? positional_table[i] : positional_table[args_idx];
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
                keyword_modulus(hash(name.data()))
            ];
            if (callback.name == name) {
                return callback;
            } else {
                if constexpr (has_kwargs) {
                    return keyword_table[kwargs_idx];
                } else {
                    return null_callback;
                }
            }
        }

        /* Produce an overload key that matches the enclosing parameter list. */
        static Params<std::array<Param, n>> parameters() {
            size_t hash = 0;
            return {
                []<size_t... Is>(std::index_sequence<Is...>, size_t& hash) {
                    return std::array<Param, n>{_parameters<Is>(hash)...};
                }(std::make_index_sequence<n>{}, hash),
                hash
            };
        }

    private:

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

        /* A tuple holding the default value for every argument in the enclosing
        parameter list that is marked as optional. */
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

            template <size_t I> requires (I < n)
            using at = Inner::template at<I>;

            /* Bind an argument list to the default values tuple using the
            sub-signature's normal Bind<> machinery. */
            template <typename... Values>
            using Bind = Inner::template Bind<Values...>;

            /* Given an index into the enclosing signature, find the corresponding index
            in the defaults tuple if that index corresponds to a default value. */
            template <size_t I> requires (ArgTraits<typename Outer::at<I>>::opt())
            static constexpr size_t find = _find<I, Tuple>;

            /* Given an index into the  */
            template <size_t I> requires (I < n)
            static constexpr size_t rfind = std::tuple_element<I, Tuple>::type::index;

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

        /// TODO: Overload tries should apply isinstance() rather than issubclass()
        /// when traversing overloads.

        /* A Trie-based data structure that describes a collection of dynamic overloads
        for a `py::Function` object, which will be dispatched to when called from
        either Python or C++. */
        struct Overloads {
            struct Edge;
            struct Metadata;
            struct Edges;
            struct Node;

        private:
            struct BoundView;

            /// TODO: type_check() should be split into two functions, one that calls
            /// issubclass and the other which calls isinstance().  The first is used
            /// when inserting a function and the second is used when calling it.  This
            /// is necessary for a Python list of the form `[1, 2, 3]` to match
            /// `List<Int>` during overloads, because falling back to the type object
            /// would erase information and yield a generic `List<Object>` type in this
            /// case.

            static bool type_check(PyObject* lhs, PyObject* rhs) {
                int rc = PyObject_IsSubclass(lhs, rhs);
                if (rc < 0) {
                    Exception::from_python();
                }
                return rc;
            }

            static bool value_check(PyObject* obj, PyObject* cls) {
                int rc = PyObject_IsInstance(obj, cls);
                if (rc < 0) {
                    Exception::from_python();
                }
                return rc;
            }

        public:

            /* A single link between two nodes in the trie, which describes how to
            traverse from one to the other.  Multiple edges may share the same target
            node, and a unique edge will be created for each parameter in a key when it
            is inserted, such that the original key can be unambiguously identified
            from a simple search of the trie structure. */
            struct Edge {
                size_t hash;
                uint64_t mask;
                std::string name;
                Object type;
                ArgKind kind;
                std::shared_ptr<Node> node;
            };

            /* An encoded representation of a function that has been inserted into the
            overload trie, which includes the function itself, a hash of the key that
            it was inserted under, a bitmask of the required arguments that must be
            satisfied to invoke the function, and a canonical path of edges starting
            from the root node that leads to the terminal function.

            These are stored in an associative set rather than a hash set in order to
            ensure address stability over the lifetime of the corresponding nodes, so
            that they don't have to manage any memory themselves. */
            struct Metadata {
                size_t hash;
                uint64_t required;
                Object func;
                std::vector<Edge> path;
                friend bool operator<(const Metadata& lhs, const Metadata& rhs) {
                    return lhs.hash < rhs.hash;
                }
                friend bool operator<(const Metadata& lhs, size_t rhs) {
                    return lhs.hash < rhs;
                }
                friend bool operator<(size_t lhs, const Metadata& rhs) {
                    return lhs < rhs.hash;
                }
            };

            /* A collection of edges linking nodes within the trie.  The edges are
            topologically sorted by their expected type, with subclasses coming before
            their parent classes, and then by their kind, with required arguments
            coming before optional arguments, which come before variadic arguments.
            The stored edges are non-owning references to the contents of the
            `Metadata::path` sequence, which is guaranteed to have a stable address. */
            struct Edges {
            private:
                friend BoundView;

                /* The topologically-sorted type map needs to store an additional
                mapping layer to account for overlapping edges and ordering based on
                kind.  By allowing transparent comparisons, we can support direct
                lookups by hash without breaking proper order. */
                struct Table {
                    struct Ptr {
                        Edge* edge;
                        Ptr(const Edge* edge = nullptr) : edge(edge) {}
                        operator const Edge*() const { return edge; }
                        const Edge& operator*() const { return *edge; }
                        const Edge* operator->() const { return edge; }
                        friend bool operator<(const Ptr& lhs, const Ptr& rhs) {
                            return
                                lhs.edge->kind < rhs.edge->kind ||
                                lhs.edge->hash < rhs.edge->hash;
                        }
                        friend bool operator<(const Ptr& lhs, size_t rhs) {
                            return lhs.edge->hash < rhs;
                        }
                        friend bool operator<(size_t lhs, const Ptr& rhs) {
                            return lhs < rhs.edge->hash;
                        }
                    };
                    std::shared_ptr<Node> node;
                    using Set = std::set<const Ptr, std::less<>>;
                    Set set;
                };

                struct TopoSort {
                    static bool operator()(PyObject* lhs, PyObject* rhs) {
                        return type_check(lhs, rhs) || lhs < rhs;
                    }
                };

                using Map = std::map<PyObject*, Table, TopoSort>;

                /* A range adaptor that only yields edges matching a particular key,
                identified by its hash. */
                struct HashView {
                    const Edges& self;
                    PyObject* type;
                    size_t hash;

                    struct Sentinel;

                    struct Iterator {
                        using iterator_category = std::input_iterator_tag;
                        using difference_type = std::ptrdiff_t;
                        using value_type = const Edge*;
                        using pointer = value_type*;
                        using reference = value_type&;

                        Map::iterator it;
                        Map::iterator end;
                        PyObject* type;
                        size_t hash;
                        const Edge* curr;

                        Iterator(
                            Map::iterator&& it,
                            Map::iterator&& end,
                            PyObject* type,
                            size_t hash
                        ) : it(std::move(it)), end(std::move(end)), type(type),
                            hash(hash), curr(nullptr)
                        {
                            while (this->it != this->end) {
                                if (type_check(type, this->it->first)) {
                                    auto lookup = this->it->second.set.find(hash);
                                    if (lookup != this->it->second.set.end()) {
                                        curr = *lookup;
                                        break;
                                    }
                                }
                                ++it;
                            }
                        }

                        Iterator& operator++() {
                            ++it;
                            while (it != end) {
                                if (type_check(type, it->first)) {
                                    auto lookup = it->second.set.find(hash);
                                    if (lookup != it->second.set.end()) {
                                        curr = *lookup;
                                        break;
                                    }
                                }
                                ++it;
                            }
                            return *this;
                        }

                        const Edge* operator*() const {
                            return curr;
                        }

                        bool operator==(const Sentinel& sentinel) const {
                            return it == end;
                        }

                        bool operator!=(const Sentinel& sentinel) const {
                            return it != end;
                        }

                    };

                    struct Sentinel {
                        bool operator==(const Iterator& iter) const {
                            return iter.it == iter.end;
                        }
                        bool operator!=(const Iterator& iter) const {
                            return iter.it != iter.end;
                        }
                    };

                    Iterator begin() const {
                        return Iterator(
                            self.map.begin(),
                            self.map.end(),
                            type,
                            hash
                        );
                    }

                    Sentinel end() const {
                        return {};
                    }

                };

                /* A range adaptor that yields edges in order, regardless of key. */
                struct OrderedView {
                    const Edges& self;
                    PyObject* type;

                    struct Sentinel;

                    struct Iterator {
                        using iterator_category = std::input_iterator_tag;
                        using difference_type = std::ptrdiff_t;
                        using value_type = const Edge*;
                        using pointer = value_type*;
                        using reference = value_type&;

                        Map::iterator it;
                        Map::iterator end;
                        Table::Set::iterator edge_it;
                        Table::Set::iterator edge_end;
                        PyObject* type;

                        Iterator(
                            Map::iterator&& it,
                            Map::iterator&& end,
                            PyObject* type
                        ) : it(std::move(it)), end(std::move(end)), type(type)
                        {
                            while (this->it != this->end) {
                                if (type_check(type, this->it->first)) {
                                    edge_it = this->it->second.set.begin();
                                    edge_end = this->it->second.set.end();
                                    break;
                                }
                                ++it;
                            }
                        }

                        Iterator& operator++() {
                            ++edge_it;
                            if (edge_it == edge_end) {
                                ++it;
                                while (it != end) {
                                    if (type_check(type, it->first)) {
                                        edge_it = it->second.set.begin();
                                        edge_end = it->second.set.end();
                                        break;
                                    }
                                    ++it;
                                }
                            }
                            return *this;
                        }

                        const Edge* operator*() const {
                            return *edge_it;
                        }

                        bool operator==(const Sentinel& sentinel) const {
                            return it == end;
                        }

                        bool operator!=(const Sentinel& sentinel) const {
                            return it != end;
                        }

                    };

                    struct Sentinel {
                        bool operator==(const Iterator& iter) const {
                            return iter.it == iter.end;
                        }
                        bool operator!=(const Iterator& iter) const {
                            return iter.it != iter.end;
                        }
                    };

                    Iterator begin() const {
                        return Iterator(
                            self.map.begin(),
                            self.map.end(),
                            type
                        );
                    }

                    Sentinel end() const {
                        return {};
                    }

                };

            public:
                Map map;

                /* Insert an edge into this map and initialize its node pointer.
                Returns true if the insertion resulted in the creation of a new node,
                or false if the edge references an existing node. */
                [[maybe_unused]] bool insert(Edge& edge) {
                    auto [outer, inserted] = map.try_emplace(
                        edge->type,
                        Table{}
                    );
                    auto [inner, success] = outer->second.set.insert(&edge);
                    if (!success) {
                        if (inserted) {
                            map.erase(outer);
                        }
                        throw TypeError(
                            "overload trie already contains an edge for type: " +
                            repr(edge.type)
                        );
                    }
                    if (inserted) {
                        outer->second.node = std::make_shared<Node>();
                    }
                    edge.node = outer->second.node;
                    return inserted;
                }

                /* Insert an edge into this map using an explicit node pointer.
                Returns true if the insertion created a new table in the map, or false
                if it was already present.  Does NOT initialize the edge's node
                pointer, and a false return value does NOT guarantee that the existing
                table references the same node. */
                [[maybe_unused]] bool insert(Edge& edge, std::shared_ptr<Node> node) {
                    auto [outer, inserted] = map.try_emplace(
                        edge->type,
                        Table{node}
                    );
                    auto [inner, success] = outer->second.set.insert(&edge);
                    if (!success) {
                        if (inserted) {
                            map.erase(outer);
                        }
                        throw TypeError(
                            "overload trie already contains an edge for type: " +
                            repr(edge.type)
                        );
                    }
                    return inserted;
                }

                /* Remove any outgoing edges from the map that match the given hash. */
                void remove(size_t hash) noexcept {
                    std::vector<PyObject*> dead;
                    for (auto& [type, table] : map) {
                        table.set.erase(hash);
                        if (table.set.empty()) {
                            dead.push_back(type);
                        }
                    }
                    for (PyObject* type : dead) {
                        map.erase(type);
                    }
                }

                /* Return a range adaptor that iterates over the topologically-sorted
                types and yields individual edges for those that match against an
                observed type.  If multiple edges exist for a given type, then the
                range will yield them in order based on kind, with required arguments
                coming before optional, which come before variadic.  There is no
                guarantee that the edges come from a single key, just that they match
                the observed type. */
                OrderedView match(PyObject* type) const {
                    return {*this, type};
                }

                /* Return a range adaptor that iterates over the topologically-sorted
                types, and yields individual edges for those that match against an
                observed type and originate from the specified key, identified by its
                unique hash.  Rather than matching all possible edges, this view will
                essentially trace out the specified key, only checking edges that are
                contained within it. */
                HashView match(PyObject* type, size_t hash) const {
                    return {*this, type, hash};
                }

            };

            /* A single node in the overload trie, which holds the topologically-sorted
            edge maps necessary for traversal, insertion, and deletion of candidate
            functions, as well as a (possibly null) terminal function to call if this
            node is the last in a given argument list. */
            struct Node {
                PyObject* func = nullptr;

                /* A sorted map of outgoing edges for positional arguments that can be
                given immediately after this node. */
                Edges positional;

                /* A map of keyword argument names to sorted maps of outgoing edges for
                the arguments that can follow this node.  A special empty string will
                be used to represent variadic keyword arguments, which can match any
                unrecognized names. */
                std::unordered_map<std::string_view, Edges> keyword;

                /* Recursively search for a matching function in this node's sub-trie.
                Returns a borrowed reference to a terminal function in the case of a
                match, or null if no match is found, which causes the algorithm to
                backtrack one level and continue searching. */
                template <typename Container>
                [[nodiscard]] PyObject* search(
                    const Params<Container>& key,
                    size_t idx,
                    uint64_t& mask,
                    size_t& hash
                ) const {
                    if (idx >= key.size()) {
                        return func;
                    }
                    const Param& param = key[idx];

                    /// NOTE: if the index is zero, then the hash is ambiguous, so we
                    /// need to test all edges in order to find a matching key.
                    /// Otherwise, we already know which key we're tracing, so we can
                    /// restrict our search to exact matches.  This maintains
                    /// consistency in the final bitmasks, since each recursive call
                    /// will only search along a single path after the first edge has
                    /// been identified, but it requires us to duplicate some logic
                    /// here, since the required view types are not interchangeable.

                    // positional arguments have empty names
                    if (param.name.empty()) {
                        if (idx) {
                            for (const Edge* edge : positional.match(param.type, hash)) {
                                size_t i = idx + 1;
                                // variadic positional arguments will test all
                                // remaining positional args against the expected type
                                // and only recur if they all match
                                if constexpr (Arguments::has_args) {
                                    if (edge->kind.variadic()) {
                                        const Param* curr;
                                        while (
                                            i < key.size() &&
                                            (curr = &key[i])->pos() &&
                                            type_check(
                                                curr->type,
                                                ptr(edge->type)
                                            )
                                        ) {
                                            ++i;
                                        }
                                        if (i < key.size() && curr->pos()) {
                                            continue;  // failed comparison
                                        }
                                    }
                                }
                                uint64_t temp_mask = mask | edge->mask;
                                size_t temp_hash = edge->hash;
                                PyObject* result = edge->node->search(
                                    key,
                                    i,
                                    temp_mask,
                                    temp_hash
                                );
                                if (result) {
                                    mask = temp_mask;
                                    hash = temp_hash;
                                    return result;
                                }
                            }
                        } else {
                            for (const Edge* edge : positional.match(param.type)) {
                                size_t i = idx + 1;
                                if constexpr (Arguments::has_args) {
                                    if (edge->kind.variadic()) {
                                        const Param* curr;
                                        while (
                                            i < key.size() &&
                                            (curr = &key[i])->pos() &&
                                            type_check(
                                                curr->type,
                                                ptr(edge->type)
                                            )
                                        ) {
                                            ++i;
                                        }
                                        if (i < key.size() && curr->pos()) {
                                            continue;
                                        }
                                    }
                                }
                                uint64_t temp_mask = mask | edge->mask;
                                size_t temp_hash = edge->hash;
                                PyObject* result = edge->node->search(
                                    key,
                                    i,
                                    temp_mask,
                                    temp_hash
                                );
                                if (result) {
                                    mask = temp_mask;
                                    hash = temp_hash;
                                    return result;
                                }
                            }
                        }

                    // keyword argument names must be looked up in the keyword map.  If
                    // the keyword name is not recognized, check for a variadic keyword
                    // argument under an empty string, and continue with that.
                    } else {
                        auto it = keyword.find(param.name);
                        if (
                            it != keyword.end() ||
                            (it = keyword.find("")) != keyword.end()
                        ) {
                            if (idx) {
                                for (const Edge* edge : it->second.match(param.type, hash)) {
                                    uint64_t temp_mask = mask | edge->mask;
                                    size_t temp_hash = edge->hash;
                                    PyObject* result = edge->node->search(
                                        key,
                                        idx + 1,
                                        temp_mask,
                                        temp_hash
                                    );
                                    if (result) {
                                        // Keyword arguments can be given in any order,
                                        // so the return value may not reflect the
                                        // deepest node.  To consistently find the
                                        // terminal node, we compare the mask of the
                                        // current node to the mask of the edge we're
                                        // following, and substitute if it is greater.
                                        if (mask > edge->mask) {
                                            result = func;
                                        }
                                        mask = temp_mask;
                                        hash = temp_hash;
                                        return result;
                                    }
                                }
                            } else {
                                for (const Edge* edge : it->second.match(param.type)) {
                                    uint64_t temp_mask = mask | edge->mask;
                                    size_t temp_hash = edge->hash;
                                    PyObject* result = edge->node->search(
                                        key,
                                        idx + 1,
                                        temp_mask,
                                        temp_hash
                                    );
                                    if (result) {
                                        if (mask > edge->mask) {
                                            result = func;
                                        }
                                        mask = temp_mask;
                                        hash = temp_hash;
                                        return result;
                                    }
                                }
                            }
                        }
                    }

                    // return nullptr to backtrack
                    return nullptr;
                }

                /* Remove all outgoing edges that match a particular hash. */
                void remove(size_t hash) {
                    positional.remove(hash);

                    std::vector<std::string_view> dead;
                    for (auto& [name, edges] : keyword) {
                        edges.remove(hash);
                        if (edges.map.empty()) {
                            dead.push_back(name);
                        }
                    }
                    for (std::string_view name : dead) {
                        keyword.erase(name);
                    }
                }

                /* Check to see if this node has any outgoing edges. */
                bool empty() const {
                    return positional.map.empty() && keyword.empty();
                }

            };

            std::shared_ptr<Node> root;
            std::set<const Metadata, std::less<>> data;
            mutable std::unordered_map<size_t, PyObject*> cache;

            /* Search the overload trie for a matching signature.  This will
            recursively backtrack until a matching node is found or the trie is
            exhausted, returning nullptr on a failed search.  The results will be
            cached for subsequent invocations.  An error will be thrown if the key does
            not fully satisfy the enclosing parameter list.  Note that variadic
            parameter packs must be expanded prior to calling this function.

            The Python-level call operator for `py::Function<>` will immediately
            delegate to this function after constructing a key from the input
            arguments, so it will be called every time a C++ function is invoked from
            Python.  If it returns null, then the fallback implementation will be
            used instead.

            Returns a borrowed reference to the terminal function if a match is
            found within the trie, or null otherwise. */
            template <typename Container>
            [[nodiscard]] PyObject* search(const Params<Container>& key) const {
                /// TODO: this also doesn't work because the parameters always contain
                /// just the parameter type.  This will mean any time the function is
                /// called with a list, it will choose the same overload, regardless of
                /// the contents of the list.  There's no way around this.

                // check the cache first
                auto it = cache.find(key.hash);
                if (it != cache.end()) {
                    return it->second;
                }

                // ensure the key minimally satisfies the enclosing parameter list
                assert_valid_args(key);

                // search the trie for a matching node
                if (root) {
                    uint64_t mask = 0;
                    size_t hash;
                    PyObject* result = root->search(key, 0, mask, hash);
                    if (result) {
                        // hash is only initialized if the key has at least one param
                        if (key.empty()) {
                            cache[key.hash] = result;  // may be null
                            return result;
                        }
                        const Metadata& metadata = *(data.find(hash));
                        if ((mask & metadata.required) == metadata.required) {
                            cache[key.hash] = result;  // may be null
                            return result;
                        }
                    }
                }
                cache[key.hash] = nullptr;
                return nullptr;
            }

            /* Search the overload trie for a matching signature, suppressing errors
            caused by the signature not satisfying the enclosing parameter list.  This
            is equivalent to calling `search()` in a try/catch, but without any error
            handling overhead.  Errors are converted into a null optionals, separate
            from the null status of the wrapped pointer, which retains the same
            semantics as `search()`.

            This is used by the Python-level `__getitem__` and `__contains__` operators
            for `py::Function<>` instances, which converts a null optional result into
            `None` on the Python side.  Otherwise, it will return the function that
            would be invoked if the function were to be called with the given
            arguments, which may be a self-reference if the fallback implementation is
            selected.

            Returns a borrowed reference to the terminal function if a match is
            found. */
            template <typename Container>
            [[nodiscard]] std::optional<PyObject*> get(
                const Params<Container>& key
            ) const {
                auto it = cache.find(key.hash);
                if (it != cache.end()) {
                    return it->second;
                }

                uint64_t mask = 0;
                for (size_t i = 0, n = key.size(); i < n; ++i) {
                    const Param& param = key[i];
                    if (param.name.empty()) {
                        const Callback& callback = Arguments::callback(i);
                        if (!callback || !callback(param.type)) {
                            return std::nullopt;
                        }
                        mask |= callback.mask;
                    } else {
                        const Callback& callback = Arguments::callback(param.name);
                        if (
                            !callback ||
                            (mask & callback.mask) ||
                            !callback(param.type)
                        ) {
                            return std::nullopt;
                        }
                        mask |= callback.mask;
                    }
                }
                if ((mask & required) != required) {
                    return std::nullopt;
                }

                if (root) {
                    mask = 0;
                    size_t hash;
                    PyObject* result = root->search(key, 0, mask, hash);
                    if (result) {
                        if (key.empty()) {
                            cache[key.hash] = result;
                            return result;
                        }
                        const Metadata& metadata = *(data.find(hash));
                        if ((mask & metadata.required) == metadata.required) {
                            cache[key.hash] = result;
                            return result;
                        }
                    }
                }
                cache[key.hash] = nullptr;
                return nullptr;
            }

            /* Filter the overload trie for a given first positional argument, which
            represents the type of the implicit `self` parameter for a bound member
            function.  Returns a range adaptor that extracts only the matching
            functions from the metadata set, with extra information encoding their full
            path through the overload trie. */
            [[nodiscard]] BoundView match(PyObject* type) const {
                return {*this, type};
            }

            /* Insert a function into the overload trie, throwing a TypeError if it
            does not conform to the enclosing parameter list or if it conflicts with
            another node in the trie. */
            template <typename Container>
            void insert(const Params<Container>& key, const Object& func) {
                // assert the key minimally satisfies the enclosing parameter list
                []<size_t... Is>(std::index_sequence<Is...>, const Params<Container>& key) {
                    size_t idx = 0;
                    (assert_viable_overload<Is>(key, idx), ...);
                }(std::make_index_sequence<Arguments::n>{}, key);

                // construct the root node if it doesn't already exist
                if (root == nullptr) {
                    root = std::make_shared<Node>();
                }

                // if the key is empty, then the root node is the terminal node
                if (key.empty()) {
                    if (root->func) {
                        throw TypeError("overload already exists");
                    }
                    root->func = ptr(func);
                    data.emplace(key.hash, 0, func, {});
                    cache.clear();
                    return;
                }

                // insert an edge linking each parameter in the key
                std::vector<Edge> path;
                path.reserve(key.size());
                Node* curr = root.get();
                int first_keyword = -1;
                int last_required = 0;
                uint64_t required = 0;
                for (int i = 0, end = key.size(); i < end; ++i) {
                    try {
                        const Param& param = key[i];
                        path.push_back({
                            .hash = key.hash,
                            .mask = 1ULL << i,
                            .name = param.name,
                            .type = reinterpret_borrow<Object>(param.type),
                            .kind = param.kind,
                            .node = nullptr
                        });
                        if (param.posonly()) {
                            curr->positional.insert(path.back());
                            if (!param.opt()) {
                                ++first_keyword;
                                last_required = i;
                                required |= 1ULL << i;
                            }
                        } else if (param.pos()) {
                            curr->positional.insert(path.back());
                            auto [it, _] = curr->keyword.try_emplace(param.name, Edges{});
                            it->second.insert(path.back(), path.back().node);
                            if (!param.opt()) {
                                last_required = i;
                                required |= 1ULL << i;
                            }
                        } else if (param.kw()) {
                            auto [it, _] = curr->keyword.try_emplace(param.name, Edges{});
                            it->second.insert(path.back());
                            if (!param.opt()) {
                                last_required = i;
                                required |= 1ULL << i;
                            }
                        } else if (param.args()) {
                            curr->positional.insert(path.back());
                        } else if (param.kwargs()) {
                            auto [it, _] = curr->keyword.try_emplace("", Edges{});
                            it->second.insert(path.back());
                        } else {
                            throw ValueError("invalid argument kind");
                        }
                        curr = path.back().node.get();

                    } catch (...) {
                        curr = root.get();
                        for (int j = 0; j < i; ++j) {
                            const Edge& edge = path[j];
                            curr->remove(edge.hash);
                            curr = edge.node.get();
                        }
                        if (root->empty()) {
                            root.reset();
                        }
                        throw;
                    }
                }

                // backfill the terminal functions and full keyword maps for each node
                try {
                    std::string_view name;
                    int start = key.size() - 1;
                    for (int i = start; i > first_keyword; --i) {
                        Edge& edge = path[i];
                        if (i >= last_required) {
                            if (edge.node->func) {
                                throw TypeError("overload already exists");
                            }
                            edge.node->func = ptr(func);
                        }
                        for (int j = first_keyword; j < key.size(); ++j) {
                            Edge& kw = path[j];
                            if (
                                kw.posonly() ||
                                kw.args() ||
                                kw.name == edge.name ||  // incoming edge
                                (i < start && kw.name == name)  // outgoing edge
                            ) {
                                continue;
                            }
                            auto& [it, _] = edge.node->keyword.try_emplace(
                                kw.name,
                                Edges{}
                            );
                            it->second.insert(kw, kw.node);
                        }
                        name = edge.name;
                    }

                    // extend backfill to the root node
                    if (!required) {
                        if (root->func) {
                            throw TypeError("overload already exists");
                        }
                        root->func = ptr(func);
                    }
                    bool extend_keywords = true;
                    for (Edge& edge : path) {
                        if (!edge.posonly()) {
                            break;
                        } else if (!edge.opt()) {
                            extend_keywords = false;
                            break;
                        }
                    }
                    if (extend_keywords) {
                        for (int j = first_keyword; j < key.size(); ++j) {
                            Edge& kw = path[j];
                            if (kw.posonly() || kw.args()) {
                                continue;
                            }
                            auto& [it, _] = root->keyword.try_emplace(
                                kw.name,
                                Edges{}
                            );
                            it->second.insert(kw, kw.node);
                        }
                    }

                } catch (...) {
                    Node* curr = root.get();
                    for (int i = 0, end = key.size(); i < end; ++i) {
                        const Edge& edge = path[i];
                        curr->remove(edge.hash);
                        if (i >= last_required) {
                            edge.node->func = nullptr;
                        }
                        curr = edge.node.get();
                    }
                    if (root->empty()) {
                        root.reset();
                    }
                    throw;
                }

                // track the function and required arguments for the inserted key
                data.emplace(key.hash, required, func, std::move(path));
                cache.clear();
            }

            /* Remove a node from the overload trie and prune any dead-ends that lead
            to it.  Returns the function that was removed, or nullopt if no matching
            function was found. */
            template <typename Container>
            [[maybe_unused]] std::optional<Object> remove(const Params<Container>& key) {
                // assert the key minimally satisfies the enclosing parameter list
                assert_valid_args(key);

                // search the trie for a matching node
                if (root) {
                    uint64_t mask = 0;
                    size_t hash;
                    Object result = reinterpret_borrow<Object>(
                        root->search(key, 0, mask, hash)
                    );
                    if (!result.is(nullptr)) {
                        // hash is only initialized if the key has at least one param
                        if (key.empty()) {
                            for (const Metadata& data : this->data) {
                                if (data.func == ptr(result)) {
                                    hash = data.hash;
                                    break;
                                }
                            }
                        }
                        const Metadata& metadata = *(data.find(hash));
                        if ((mask & metadata.required) == metadata.required) {
                            Node* curr = root.get();
                            for (const Edge& edge : metadata.path) {
                                curr->remove(edge.hash);
                                if (edge.node->func == ptr(metadata.func)) {
                                    edge.node->func = nullptr;
                                }
                                curr = edge.node.get();
                            }
                            if (root->func == ptr(metadata.func)) {
                                root->func = nullptr;
                            }
                            data.erase(hash);
                            if (data.empty()) {
                                root.reset();
                            }
                            return result;
                        }
                    }
                }
                return std::nullopt;
            }

            /* Clear the overload trie, removing all tracked functions. */
            void clear() {
                cache.clear();
                root.reset();
                data.clear();
            }

            /* Manually reset the function's overload cache, forcing overload paths to
            be recalculated on subsequent calls. */
            void flush() {
                cache.clear();
            }

        private:

            /* A range adaptor that iterates over the space of overloads that follow
            from a given `self` argument, which is used to prune the trie.  When a
            bound method is created, it will use one of these views to correctly
            forward the overload interface. */
            struct BoundView {
                const Overloads& self;
                PyObject* type;

                struct Sentinel;

                struct Iterator {
                    using iterator_category = std::input_iterator_tag;
                    using difference_type = std::ptrdiff_t;
                    using value_type = const Metadata;
                    using pointer = value_type*;
                    using reference = value_type&;

                    const Overloads& self;
                    const Metadata* curr;
                    Edges::OrderedView view;
                    std::ranges::iterator_t<typename Edges::OrderedView> it;
                    std::ranges::sentinel_t<typename Edges::OrderedView> end;
                    std::unordered_set<size_t> visited;

                    Iterator(const Overloads& self, PyObject* type) :
                        self(self),
                        curr(nullptr),
                        view(self.root->positional.match(type)),
                        it(std::ranges::begin(this->view)),
                        end(std::ranges::end(this->view))
                    {
                        if (it != end) {
                            curr = self.data.find((*it)->hash);
                            visited.insert(curr->hash);
                        }
                    }

                    Iterator& operator++() {
                        while (++it != end) {
                            const Edge* edge = *it;
                            auto lookup = visited.find(edge->hash);
                            if (lookup == visited.end()) {
                                visited.insert(edge->hash);
                                curr = &*(self.data.find(edge->hash));
                                return *this;
                            }
                        }
                        return *this;
                    }

                    const Metadata& operator*() const {
                        return *curr;
                    }

                    bool operator==(const Sentinel& sentinel) const {
                        return it == end;
                    }

                    bool operator!=(const Sentinel& sentinel) const {
                        return it != end;
                    }

                };

                struct Sentinel {
                    bool operator==(const Iterator& iter) const {
                        return iter.it == iter.end;
                    }
                    bool operator!=(const Iterator& iter) const {
                        return iter.it != iter.end;
                    }
                };

                Iterator begin() const {
                    return Iterator{self, type};
                }

                Sentinel end() const {
                    return Sentinel{};
                }
            };

            template <typename Container>
            static void assert_valid_args(const Params<Container>& key) {
                uint64_t mask = 0;
                for (size_t i = 0, n = key.size(); i < n; ++i) {
                    const Param& param = key[i];
                    if (param.name.empty()) {
                        const Callback& callback = Arguments::callback(i);
                        if (!callback) {
                            throw TypeError(
                                "received unexpected positional argument at index " +
                                std::to_string(i)
                            );
                        }
                        if (!callback(param.type)) {
                            throw TypeError(
                                "expected positional argument at index " +
                                std::to_string(i) + " to be a subclass of '" +
                                repr(reinterpret_borrow<Object>(
                                    reinterpret_cast<PyObject*>(callback.type())
                                )) + "', not: '" + repr(reinterpret_borrow<Object>(
                                    reinterpret_cast<PyObject*>(param.type)
                                )) + "'"
                            );
                        }
                        mask |= callback.mask;
                    } else if (param.kw()) {
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
                                "' to be a subclass of '" +
                                repr(reinterpret_borrow<Object>(
                                    reinterpret_cast<PyObject*>(callback.type())
                                )) + "', not: '" + repr(reinterpret_borrow<Object>(
                                    reinterpret_cast<PyObject*>(param.type)
                                )) + "'"
                            );
                        }
                        mask |= callback.mask;
                    }
                }
                if ((mask & Arguments::required) != Arguments::required) {
                    uint64_t missing = Arguments::required & ~(mask & Arguments::required);
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
            }

            template <size_t I, typename Container>
            static void assert_viable_overload(
                const Params<Container>& key,
                size_t& idx
            ) {
                using T = __cast__<std::remove_cvref_t<typename ArgTraits<at<I>>::type>>;

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

                if constexpr (ArgTraits<at<I>>::posonly()) {
                    if (idx >= key.size()) {
                        if (ArgTraits<at<I>>::name.empty()) {
                            throw TypeError(
                                "missing positional-only argument at index " +
                                std::to_string(idx)
                            );
                        } else {
                            throw TypeError(
                                "missing positional-only argument '" +
                                ArgTraits<at<I>>::name + "' at index " +
                                std::to_string(idx)
                            );
                        }
                    }
                    const Param& param = key[idx];
                    if (!param.posonly()) {
                        if (ArgTraits<at<I>>::name.empty()) {
                            throw TypeError(
                                "expected positional-only argument at index " +
                                std::to_string(idx) + ", not " + description(param)
                            );
                        } else {
                            throw TypeError(
                                "expected argument '" + ArgTraits<at<I>>::name +
                                "' at index " + std::to_string(idx) +
                                " to be positional-only, not " + description(param)
                            );
                        }
                    }
                    if (
                        !ArgTraits<at<I>>::name.empty() &&
                        param.name != ArgTraits<at<I>>::name
                    ) {
                        throw TypeError(
                            "expected argument '" + ArgTraits<at<I>>::name +
                            "' at index " + std::to_string(idx) + ", not '" +
                            std::string(param.name) + "'"
                        );
                    }
                    if (!ArgTraits<at<I>>::opt() && param.opt()) {
                        if (ArgTraits<at<I>>::name.empty()) {
                            throw TypeError(
                                "required positional-only argument at index " +
                                std::to_string(idx) + " must not have a default "
                                "value"
                            );
                        } else {
                            throw TypeError(
                                "required positional-only argument '" +
                                ArgTraits<at<I>>::name + "' at index " +
                                std::to_string(idx) + " must not have a default "
                                "value"
                            );
                        }
                    }
                    if (!issubclass<T>(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(param.type)
                    ))) {
                        if (ArgTraits<at<I>>::name.empty()) {
                            throw TypeError(
                                "expected positional-only argument at index " +
                                std::to_string(idx) + " to be a subclass of '" +
                                repr(reinterpret_borrow<Object>(
                                    reinterpret_cast<PyObject*>(ptr(Type<T>()))
                                )) + "', not: '" + repr(reinterpret_borrow<Object>(
                                    reinterpret_cast<PyObject*>(param.type)
                                )) + "'"
                            );
                        } else {
                            throw TypeError(
                                "expected positional-only argument '" +
                                ArgTraits<at<I>>::name + "' at index " +
                                std::to_string(idx) + " to be a subclass of '" +
                                repr(reinterpret_borrow<Object>(
                                    reinterpret_cast<PyObject*>(ptr(Type<T>()))
                                )) + "', not: '" + repr(reinterpret_borrow<Object>(
                                    reinterpret_cast<PyObject*>(param.type)
                                )) + "'"
                            );
                        }
                    }
                    ++idx;

                } else if constexpr (ArgTraits<at<I>>::pos()) {
                    if (idx >= key.size()) {
                        throw TypeError(
                            "missing positional-or-keyword argument '" +
                            ArgTraits<at<I>>::name + "' at index " +
                            std::to_string(idx)
                        );
                    }
                    const Param& param = key[idx];
                    if (!param.pos() || !param.kw()) {
                        throw TypeError(
                            "expected argument '" + ArgTraits<at<I>>::name +
                            "' at index " + std::to_string(idx) +
                            " to be positional-or-keyword, not " + description(param)
                        );
                    }
                    if (param.name != ArgTraits<at<I>>::name) {
                        throw TypeError(
                            "expected positional-or-keyword argument '" +
                            ArgTraits<at<I>>::name + "' at index " +
                            std::to_string(idx) + ", not '" +
                            std::string(param.name) + "'"
                        );
                    }
                    if (!ArgTraits<at<I>>::opt() && param.opt()) {
                        throw TypeError(
                            "required positional-or-keyword argument '" +
                            ArgTraits<at<I>>::name + "' at index " +
                            std::to_string(idx) + " must not have a default value"
                        );
                    }
                    if (!issubclass<T>(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(param.type)
                    ))) {
                        throw TypeError(
                            "expected positional-or-keyword argument '" +
                            ArgTraits<at<I>>::name + "' at index " +
                            std::to_string(idx) + " to be a subclass of '" +
                            repr(reinterpret_borrow<Object>(
                                reinterpret_cast<PyObject*>(ptr(Type<T>()))
                            )) + "', not: '" + repr(reinterpret_borrow<Object>(
                                reinterpret_cast<PyObject*>(param.type)
                            )) + "'"
                        );
                    }
                    ++idx;

                } else if constexpr (ArgTraits<at<I>>::kw()) {
                    if (idx >= key.size()) {
                        throw TypeError(
                            "missing keyword-only argument '" + ArgTraits<at<I>>::name +
                            "' at index " + std::to_string(idx)
                        );
                    }
                    const Param& param = key[idx];
                    if (!param.kwonly()) {
                        throw TypeError(
                            "expected argument '" + ArgTraits<at<I>>::name +
                            "' at index " + std::to_string(idx) +
                            " to be keyword-only, not " + description(param)
                        );
                    }
                    if (param.name != ArgTraits<at<I>>::name) {
                        throw TypeError(
                            "expected keyword-only argument '" + ArgTraits<at<I>>::name +
                            "' at index " + std::to_string(idx) + ", not '" +
                            std::string(param.name) + "'"
                        );
                    }
                    if (!ArgTraits<at<I>>::opt() && param.opt()) {
                        throw TypeError(
                            "required keyword-only argument '" + ArgTraits<at<I>>::name +
                            "' at index " + std::to_string(idx) + " must not have a "
                            "default value"
                        );
                    }
                    if (!issubclass<T>(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(param.type)
                    ))) {
                        throw TypeError(
                            "expected keyword-only argument '" + ArgTraits<at<I>>::name +
                            "' at index " + std::to_string(idx) +
                            " to be a subclass of '" + repr(reinterpret_borrow<Object>(
                                reinterpret_cast<PyObject*>(ptr(Type<T>()))
                            )) + "', not: '" + repr(reinterpret_borrow<Object>(
                                reinterpret_cast<PyObject*>(param.type)
                            )) + "'"
                        );
                    }
                    ++idx;

                } else if constexpr (ArgTraits<at<I>>::args()) {
                    while (idx < key.size()) {
                        const Param& param = key[idx];
                        if (!(param.pos() || param.args())) {
                            break;
                        }
                        if (!issubclass<T>(
                            reinterpret_borrow<Object>(
                                reinterpret_cast<PyObject*>(param.type)
                            )
                        )) {
                            if (param.name.empty()) {
                                throw TypeError(
                                    "expected variadic positional argument at index " +
                                    std::to_string(idx) + " to be a subclass of '" +
                                    repr(reinterpret_borrow<Object>(
                                        reinterpret_cast<PyObject*>(ptr(Type<T>()))
                                    )) + "', not: '" + repr(reinterpret_borrow<Object>(
                                        reinterpret_cast<PyObject*>(param.type)
                                    )) + "'"
                                );
                            } else {
                                throw TypeError(
                                    "expected variadic positional argument '" +
                                    std::string(param.name) + "' at index " +
                                    std::to_string(idx) + " to be a subclass of '" +
                                    repr(reinterpret_borrow<Object>(
                                        reinterpret_cast<PyObject*>(ptr(Type<T>()))
                                    )) + "', not: '" + repr(reinterpret_borrow<Object>(
                                        reinterpret_cast<PyObject*>(param.type)
                                    )) + "'"
                                );
                            }
                        }
                        ++idx;
                    }

                } else if constexpr (ArgTraits<at<I>>::kwargs()) {
                    while (idx < key.size()) {
                        const Param& param = key[idx];
                        if (!(param.kw() || param.kwargs())) {
                            break;
                        }
                        if (!issubclass<T>(
                            reinterpret_borrow<Object>(
                                reinterpret_cast<PyObject*>(param.type)
                            )
                        )) {
                            throw TypeError(
                                "expected variadic keyword argument '" +
                                std::string(param.name) + "' at index " +
                                std::to_string(idx) + " to be a subclass of '" +
                                repr(reinterpret_borrow<Object>(
                                    reinterpret_cast<PyObject*>(ptr(Type<T>()))
                                )) + "', not: '" + repr(reinterpret_borrow<Object>(
                                    reinterpret_cast<PyObject*>(param.type)
                                )) + "'"
                            );
                        }
                        ++idx;
                    }

                } else {
                    static_assert(false, "invalid argument kind");
                }
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
                                ArgTraits<typename Inner::template at<J>>::kw()
                            )
                        ) {
                            return false;
                        }
                    } else if constexpr (ArgTraits<T>::pos()) {
                        if constexpr (
                            (
                                ArgTraits<typename Inner::template at<J>>::pos() &&
                                Inner::template has<ArgTraits<T>::name>
                            ) || (
                                ArgTraits<typename Inner::template at<J>>::kw() &&
                                !ArgTraits<T>::opt() &&
                                !Inner::template has<ArgTraits<T>::name>
                            )
                        ) {
                            return false;
                        }
                    } else if constexpr (ArgTraits<T>::kwonly()) {
                        if constexpr (
                            ArgTraits<typename Inner::template at<J>>::pos() || (
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
                            release(template_string<name>())
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

            template <size_t I> requires (I < n)
            using at = Inner::template at<I>;

            /* Call operator is only enabled if source arguments are well-formed and
            match the target signature. */
            static constexpr bool enable =
                Inner::proper_argument_order &&
                Inner::no_duplicate_arguments &&
                enable_recursive<0, 0>();

            /// TODO: both of these call operators need to be updated to handle the
            /// `self` parameter.

            /* Invoke a C++ function from C++ using Python-style arguments. */
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
                        return func({
                            cpp_to_cpp<Is>(
                                defaults,
                                kwargs,
                                std::forward<Values>(values)...
                            )
                        }...);

                    // interpose the two if there are both positional and keyword argument packs
                    } else {
                        return func({
                            cpp_to_cpp<Is>(
                                defaults,
                                std::forward<Source>(values)...
                            )
                        }...);
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

            /* Invoke a Python function from C++ using Python-style arguments.  This
            will always return a new reference to a raw Python object, or throw a
            runtime error if the arguments are malformed in some way. */
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
    struct to_member_func {
        static constexpr bool enable = false;
    };
    template <typename R, typename... A, typename Self>
        requires (std::is_void_v<std::remove_cvref_t<Self>>)
    struct to_member_func<R(*)(A...), Self> {
        static constexpr bool enable = true;
        using type = R(*)(A...);
    };
    template <typename R, typename... A, typename Self>
        requires (std::is_void_v<std::remove_cvref_t<Self>>)
    struct to_member_func<R(*)(A...) noexcept, Self> {
        static constexpr bool enable = true;
        using type = R(*)(A...) noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...);
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) &;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) & noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) &&;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) && noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), const Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, const Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), const Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const &;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, const Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const & noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), const Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const &&;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, const Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const && noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), volatile Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, volatile Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), volatile Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile &;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, volatile Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile & noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), volatile Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile &&;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, volatile Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) volatile && noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), const volatile Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, const volatile Self> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), const volatile Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile &;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, const volatile Self&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile & noexcept;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...), const volatile Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile &&;
    };
    template <typename R, typename... A, typename Self>
    struct to_member_func<R(*)(A...) noexcept, const volatile Self&&> {
        static constexpr bool enable = true;
        using type = R(std::remove_cvref_t<Self>::*)(A...) const volatile && noexcept;
    };

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
        template <typename T>
        static constexpr bool can_make_member = to_member_func<R(*)(A...), T>::enable;
        using Return = R;
        using Self = void;
        using to_ptr = Signature;
        using to_value = Signature<R(A...)>;
        template <typename R2>
        using with_return = Signature<R2(*)(A...)>;
        template <typename C> requires (can_make_member<C>)
        using with_self = Signature<typename to_member_func<R(*)(A...), C>::type>;
        template <typename... A2>
        using with_args = Signature<R(*)(A2...)>;
        template <typename R2, typename... A2>
        static constexpr bool compatible =
            std::convertible_to<R2, R> &&
            (Arguments<A...>::template compatible<A2> && ...);
        template <typename Func>
        static constexpr bool invocable = std::is_invocable_r_v<Func, R, A...>;
        static std::function<R(A...)> capture(PyObject* obj) {
            return [obj](A... args) -> R {
                using Call = typename Arguments<A...>::template Bind<A...>;
                PyObject* result = Call{}(obj, std::forward<A>(args)...);
                if constexpr (std::is_void_v<R>) {
                    Py_DECREF(result);
                } else {
                    return reinterpret_steal<Object>(result);
                }
            };
        }
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
        template <typename T>
        static constexpr bool can_make_member = to_member_func<R(*)(A...), T>::enable;
        using Return = R;
        using Self = C&;
        using to_ptr = Signature<R(*)(Self, A...)>;
        using to_value = Signature<R(Self, A...)>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...)>;
        template <typename C2> requires (can_make_member<C2>)
        using with_self = Signature<typename to_member_func<R(*)(A...), C2>::type>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...)>;
        template <typename R2, typename... A2>
        static constexpr bool compatible =
            std::convertible_to<R2, R> &&
            (Arguments<Self, A...>::template compatible<A2> && ...);
        template <typename Func>
        static constexpr bool invocable = std::is_invocable_r_v<Func, R, Self, A...>;
        static std::function<R(Self, A...)> capture(PyObject* obj) {
            return [obj](Self self, A... args) -> R {
                using Call = typename Arguments<Self, A...>::template Bind<Self, A...>;
                PyObject* result = Call{}(obj, self, std::forward<A>(args)...);
                if constexpr (std::is_void_v<R>) {
                    Py_DECREF(result);
                } else {
                    return reinterpret_steal<Object>(result);
                }
            };
        }
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
        template <typename T>
        static constexpr bool can_make_member = to_member_func<R(*)(A...), T>::enable;
        using Return = R;
        using Self = const C&;
        using to_ptr = Signature<R(*)(Self, A...)>;
        using to_value = Signature<R(Self, A...)>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) const>;
        template <typename C2> requires (can_make_member<C2>)
        using with_self = Signature<typename to_member_func<R(*)(A...), C2>::type>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const>;
        template <typename R2, typename... A2>
        static constexpr bool compatible =
            std::convertible_to<R2, R> &&
            (Arguments<Self, A...>::template compatible<A2> && ...);
        template <typename Func>
        static constexpr bool invocable = std::is_invocable_r_v<Func, R, Self, A...>;
        static std::function<R(Self, A...)> capture(PyObject* obj) {
            return [obj](Self self, A... args) -> R {
                using Call = typename Arguments<Self, A...>::template Bind<Self, A...>;
                PyObject* result = Call{}(obj, self, std::forward<A>(args)...);
                if constexpr (std::is_void_v<R>) {
                    Py_DECREF(result);
                } else {
                    return reinterpret_steal<Object>(result);
                }
            };
        }
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
        template <typename T>
        static constexpr bool can_make_member = to_member_func<R(*)(A...), T>::enable;
        using Return = R;
        using Self = volatile C&;
        using to_ptr = Signature<R(*)(Self, A...)>;
        using to_value = Signature<R(Self, A...)>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) volatile>;
        template <typename C2> requires (can_make_member<C2>)
        using with_self = Signature<typename to_member_func<R(*)(A...), C2>::type>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) volatile>;
        template <typename R2, typename... A2>
        static constexpr bool compatible =
            std::convertible_to<R2, R> &&
            (Arguments<Self, A...>::template compatible<A2> && ...);
        template <typename Func>
        static constexpr bool invocable = std::is_invocable_r_v<Func, R, Self, A...>;
        static std::function<R(Self, A...)> capture(PyObject* obj) {
            return [obj](Self self, A... args) -> R {
                using Call = typename Arguments<Self, A...>::template Bind<Self, A...>;
                PyObject* result = Call{}(obj, self, std::forward<A>(args)...);
                if constexpr (std::is_void_v<R>) {
                    Py_DECREF(result);
                } else {
                    return reinterpret_steal<Object>(result);
                }
            };
        }
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
        template <typename T>
        static constexpr bool can_make_member = to_member_func<R(*)(A...), T>::enable;
        using Return = R;
        using Self = const volatile C&;
        using to_ptr = Signature<R(*)(Self, A...)>;
        using to_value = Signature<R(Self, A...)>;
        template <typename R2>
        using with_return = Signature<R2(C::*)(A...) const volatile>;
        template <typename C2> requires (can_make_member<C2>)
        using with_self = Signature<typename to_member_func<R(*)(A...), C2>::type>;
        template <typename... A2>
        using with_args = Signature<R(C::*)(A2...) const volatile>;
        template <typename R2, typename... A2>
        static constexpr bool compatible =
            std::convertible_to<R2, R> &&
            (Arguments<Self, A...>::template compatible<A2> && ...);
        template <typename Func>
        static constexpr bool invocable = std::is_invocable_r_v<Func, R, Self, A...>;
        static std::function<R(Self, A...)> capture(PyObject* obj) {
            return [obj](Self self, A... args) -> R {
                using Call = typename Arguments<Self, A...>::template Bind<Self, A...>;
                PyObject* result = Call{}(obj, self, std::forward<A>(args)...);
                if constexpr (std::is_void_v<R>) {
                    Py_DECREF(result);
                } else {
                    return reinterpret_steal<Object>(result);
                }
            };
        }
    };
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile noexcept> : Signature<R(C::*)(A...) const volatile> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile &> : Signature<R(C::*)(A...) const volatile> {};
    template <typename R, typename C, typename... A>
    struct Signature<R(C::*)(A...) const volatile & noexcept> : Signature<R(C::*)(A...) const volatile> {}; 

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

    /* A structural type hint whose `isinstance()`/`issubclass()` operators only return
    true if ALL of the composite functions are present within a type's interface. */
    struct FuncIntersect : PyObject {
        Object lhs;
        Object rhs;

        explicit FuncIntersect(Object&& lhs, Object&& rhs) :
            lhs(std::move(lhs)),
            rhs(std::move(rhs))
        {}

        static void __dealloc__(FuncIntersect* self) {
            self->~FuncIntersect();
        }

        static PyObject* __instancecheck__(FuncIntersect* self, PyObject* instance) {
            int rc = PyObject_IsInstance(
                instance,
                ptr(self->lhs)
            );
            if (rc < 0) {
                return nullptr;
            } else if (!rc) {
                Py_RETURN_FALSE;
            }
            rc = PyObject_IsInstance(
                instance,
                ptr(self->rhs)
            );
            if (rc < 0) {
                return nullptr;
            } else if (!rc) {
                Py_RETURN_FALSE;
            }
            Py_RETURN_TRUE;
        }

        static PyObject* __subclasscheck__(FuncIntersect* self, PyObject* cls) {
            int rc = PyObject_IsSubclass(
                cls,
                ptr(self->lhs)
            );
            if (rc < 0) {
                return nullptr;
            } else if (!rc) {
                Py_RETURN_FALSE;
            }
            rc = PyObject_IsSubclass(
                cls,
                ptr(self->rhs)
            );
            if (rc < 0) {
                return nullptr;
            } else if (!rc) {
                Py_RETURN_FALSE;
            }
            Py_RETURN_TRUE;
        }

        static PyObject* __and__(PyObject* lhs, PyObject* rhs) {
            try {
                /// TODO: do some validation on the inputs, ensuring that they are both
                /// bertrand functions or structural intersection/union hints.

                FuncIntersect* hint = reinterpret_cast<FuncIntersect*>(
                    __type__.tp_alloc(&__type__, 0)
                );
                if (hint == nullptr) {
                    return nullptr;
                }
                try {
                    new (hint) FuncIntersect(
                        reinterpret_borrow<Object>(lhs),
                        reinterpret_borrow<Object>(rhs)
                    );
                } catch (...) {
                    Py_DECREF(hint);
                    throw;
                }
                return hint;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __repr__(FuncIntersect* self) {
            try {
                std::string str =
                    "(" + repr(self->lhs) + " & " + repr(self->rhs) + ")";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyNumberMethods number;

        inline static PyMethodDef methods[] = {
            {
                "__instancecheck__",
                reinterpret_cast<PyCFunction>(&__instancecheck__),
                METH_O,
                nullptr
            },
            {
                "__subclasscheck__",
                reinterpret_cast<PyCFunction>(&__subclasscheck__),
                METH_O,
                nullptr
            },
            {nullptr}
        };

        static PyTypeObject __type__;

    };

    /* A structural type hint whose `isinstance()`/`issubclass()` operators only return
    true if ANY of the composite functions are present within a type's interface. */
    struct FuncUnion : PyObject {
        Object lhs;
        Object rhs;

        explicit FuncUnion(Object&& lhs, Object&& rhs) :
            lhs(std::move(lhs)),
            rhs(std::move(rhs))
        {}

        static void __dealloc__(FuncUnion* self) {
            self->~FuncUnion();
        }

        static PyObject* __instancecheck__(FuncUnion* self, PyObject* instance) {
            int rc = PyObject_IsInstance(
                instance,
                ptr(self->lhs)
            );
            if (rc < 0) {
                return nullptr;
            } else if (rc) {
                Py_RETURN_TRUE;
            }
            rc = PyObject_IsInstance(
                instance,
                ptr(self->rhs)
            );
            if (rc < 0) {
                return nullptr;
            } else if (rc) {
                Py_RETURN_TRUE;
            }
            Py_RETURN_FALSE;
        }

        static PyObject* __subclasscheck__(FuncUnion* self, PyObject* cls) {
            int rc = PyObject_IsSubclass(
                cls,
                ptr(self->lhs)
            );
            if (rc < 0) {
                return nullptr;
            } else if (rc) {
                Py_RETURN_TRUE;
            }
            rc = PyObject_IsSubclass(
                cls,
                ptr(self->rhs)
            );
            if (rc < 0) {
                return nullptr;
            } else if (rc) {
                Py_RETURN_TRUE;
            }
            Py_RETURN_FALSE;
        }

        static PyObject* __or__(PyObject* lhs, PyObject* rhs) {
            try {
                /// TODO: do some validation on the inputs, ensuring that they are both
                /// bertrand functions or structural intersection/union hints.

                FuncUnion* hint = reinterpret_cast<FuncUnion*>(
                    __type__.tp_alloc(&__type__, 0)
                );
                if (hint == nullptr) {
                    return nullptr;
                }
                try {
                    new (hint) FuncUnion(
                        reinterpret_borrow<Object>(lhs),
                        reinterpret_borrow<Object>(rhs)
                    );
                } catch (...) {
                    Py_DECREF(hint);
                    throw;
                }
                return hint;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* __repr__(FuncUnion* self) {
            try {
                std::string str =
                    "(" + repr(self->lhs) + " | " + repr(self->rhs) + ")";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyNumberMethods number;

        inline static PyMethodDef methods[] = {
            {
                "__instancecheck__",
                reinterpret_cast<PyCFunction>(&__instancecheck__),
                METH_O,
                nullptr
            },
            {
                "__subclasscheck__",
                reinterpret_cast<PyCFunction>(&__subclasscheck__),
                METH_O,
                nullptr
            },
            {nullptr}
        };

        static PyTypeObject __type__;

    };

    PyNumberMethods FuncIntersect::number = {
        .nb_and = reinterpret_cast<binaryfunc>(&FuncIntersect::__and__),
        .nb_or = reinterpret_cast<binaryfunc>(&FuncUnion::__or__),
    };

    PyNumberMethods FuncUnion::number = {
        .nb_and = reinterpret_cast<binaryfunc>(&FuncIntersect::__and__),
        .nb_or = reinterpret_cast<binaryfunc>(&FuncUnion::__or__),
    };

    PyTypeObject FuncIntersect::__type__ = {
        .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
        .tp_name = typeid(FuncIntersect).name(),
        .tp_basicsize = sizeof(FuncIntersect),
        .tp_itemsize = 0,
        .tp_dealloc = reinterpret_cast<destructor>(&FuncIntersect::__dealloc__),
        .tp_repr = reinterpret_cast<reprfunc>(&FuncIntersect::__repr__),
        .tp_as_number = &FuncIntersect::number,
        .tp_flags = Py_TPFLAGS_DEFAULT,
        .tp_methods = FuncIntersect::methods,
    };

    PyTypeObject FuncUnion::__type__ = {
        .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
        .tp_name = typeid(FuncUnion).name(),
        .tp_basicsize = sizeof(FuncUnion),
        .tp_itemsize = 0,
        .tp_dealloc = reinterpret_cast<destructor>(&FuncUnion::__dealloc__),
        .tp_repr = reinterpret_cast<reprfunc>(&FuncUnion::__repr__),
        .tp_as_number = &FuncUnion::number,
        .tp_flags = Py_TPFLAGS_DEFAULT,
        .tp_methods = FuncUnion::methods,
    };

    /* A `@classmethod` descriptor for a C++ function type, which references an
    unbound function and produces bound equivalents that pass the enclosing type as a
    `self` argument when accessed. */
    struct ClassMethod : PyObject {
        static PyTypeObject __type__;

        PyObject* __wrapped__;
        PyObject* cls;
        PyObject* target;

        explicit ClassMethod(PyObject* func, PyObject* cls) :
            __wrapped__(func),
            cls(cls),
            target(release(get_member_function_type(func, cls)))
        {
            Py_INCREF(__wrapped__);
            Py_INCREF(cls);
        }

        ~ClassMethod() noexcept {
            Py_DECREF(__wrapped__);
            Py_DECREF(cls);
            Py_DECREF(target);
        }

        static void __dealloc__(ClassMethod* self) {
            self->~ClassMethod();
        }

        static PyObject* __get__(ClassMethod* self, PyObject* obj, PyObject* type) {
            PyObject* const args[] = {
                self->target,
                self->__wrapped__,
                type == Py_None ? reinterpret_cast<PyObject*>(Py_TYPE(obj)) : type,
            };
            return PyObject_VectorcallMethod(
                ptr(template_string<"_capture">()),
                args,
                3,
                nullptr
            );
        }

        static PyObject* __repr__(ClassMethod* self) {
            try {
                std::string str =
                    "<classmethod(" + repr(reinterpret_borrow<Object>(
                        self->__wrapped__
                    )) + ")>";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        /// TODO: implement this here, rather than as a forward declaration, using the
        /// same logic as __get__.

        /// TODO: this must be defined after the metaclass + template framework
        /// is figured out.
        static Object get_member_function_type(
            PyObject* func,
            PyObject* cls
        ) noexcept;

    };

    PyTypeObject ClassMethod::__type__ = {
        .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
        .tp_name = typeid(ClassMethod).name(),
        .tp_basicsize = sizeof(ClassMethod),
        .tp_itemsize = 0,
        .tp_dealloc = reinterpret_cast<destructor>(&ClassMethod::__dealloc__),
        .tp_repr = reinterpret_cast<reprfunc>(&ClassMethod::__repr__),
        .tp_flags =
            Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION |
            Py_TPFLAGS_HAVE_VECTORCALL,
        .tp_doc = PyDoc_STR(
R"doc(A descriptor that binds a C++ function as a class method of a Python class.

Notes
-----
This descriptor can only be instantiated by applying the `@classmethod`
decorator of a bertrand function to a Python type.

Note that each template instantiation exposes a unique descriptor type, which
mirrors C++ semantics and enables structural typing via `isinstance()` and
`issubclass()`.)doc"
        ),
        .tp_descr_get = reinterpret_cast<descrgetfunc>(&ClassMethod::__get__),
    };

    /* A `@staticmethod` descriptor for a C++ function type, which references an
    unbound function and directly forwards it when accessed. */
    struct StaticMethod : PyObject {
        static PyTypeObject __type__;

        PyObject* __wrapped__;

        explicit StaticMethod(PyObject* __wrapped__) noexcept :
            __wrapped__(__wrapped__)
        {
            Py_INCREF(__wrapped__);
        }

        ~StaticMethod() noexcept {
            Py_DECREF(__wrapped__);
        }

        static void __dealloc__(StaticMethod* self) noexcept {
            self->~StaticMethod();
        }

        static PyObject* __get__(
            StaticMethod* self,
            PyObject* obj,
            PyObject* type
        ) noexcept {
            return Py_NewRef(self->__wrapped__);
        }

        static PyObject* __repr__(StaticMethod* self) noexcept {
            try {
                std::string str =
                    "<staticmethod(" + repr(reinterpret_borrow<Object>(
                        self->__wrapped__
                    )) + ")>";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        static PyMemberDef members[];

    };

    PyMemberDef StaticMethod::members[] = {
        {
            "__wrapped__",
            Py_T_OBJECT_EX,
            offsetof(StaticMethod, __wrapped__),
            Py_READONLY,
            nullptr
        },
        {nullptr}
    };

    PyTypeObject StaticMethod::__type__ = {
        .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
        .tp_name = typeid(StaticMethod).name(),
        .tp_basicsize = sizeof(StaticMethod),
        .tp_itemsize = 0,
        .tp_dealloc = reinterpret_cast<destructor>(&StaticMethod::__dealloc__),
        .tp_repr = reinterpret_cast<reprfunc>(&StaticMethod::__repr__),
        .tp_flags =
            Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION |
            Py_TPFLAGS_HAVE_VECTORCALL,
        .tp_doc = PyDoc_STR(
R"doc(A descriptor that binds a C++ function into a static method of a Python
class.

Notes
-----
This descriptor can only be instantiated by applying the `@staticmethod`
decorator of a bertrand function to a Python type.

Note that each template instantiation exposes a unique descriptor type, which
mirrors C++ semantics and enables structural typing via `isinstance()` and
`issubclass()`.)doc"
        ),
        .tp_descr_get = reinterpret_cast<descrgetfunc>(&StaticMethod::__get__),
    };

    /* A `@property` descriptor for a C++ function type that accepts a single
    compatible argument, which will be used as the getter for the property.  Setters
    and deleters can also be registered with the same `self` parameter.  The setter can
    accept any type for the assigned value, allowing overloads. */
    struct Property : PyObject {
        static PyTypeObject __type__;

        PyObject* cls;
        PyObject* fget;
        PyObject* fset;
        PyObject* fdel;

        /// TODO: Properties should convert the setter/deleter into C++ functions
        /// supporting overloads, just like the getter?  I don't even really know
        /// how that would work.

        explicit Property(
            PyObject* cls,
            PyObject* fget,
            PyObject* fset,
            PyObject* fdel
        ) noexcept : cls(cls), fget(fget), fset(fset), fdel(fdel)
        {
            Py_INCREF(cls);
            Py_INCREF(fget);
            Py_XINCREF(fset);
            Py_XINCREF(fdel);
        }

        ~Property() noexcept {
            Py_DECREF(cls);
            Py_DECREF(fget);
            Py_XDECREF(fset);
            Py_XDECREF(fdel);
        }

        static void __dealloc__(Property* self) noexcept {
            self->~Property();
        }

        static PyObject* __get__(
            Property* self,
            PyObject* obj,
            PyObject* type
        ) noexcept {
            return PyObject_CallOneArg(self->fget, obj);
        }

        static PyObject* __set__(
            Property* self,
            PyObject* obj,
            PyObject* value
        ) noexcept {
            if (value) {
                if (self->fset == nullptr) {
                    PyObject* name = PyObject_GetAttr(
                        self->fget,
                        ptr(template_string<"__name__">())
                    );
                    if (name == nullptr) {
                        return nullptr;
                    }
                    PyErr_Format(
                        PyExc_AttributeError,
                        "property '%U' of %R object has no setter",
                        name,
                        self->cls
                    );
                    Py_DECREF(name);
                    return nullptr;
                }
                PyObject* const args[] = {obj, value};
                return PyObject_Vectorcall(
                    self->fset,
                    args,
                    2,
                    nullptr
                );
            }

            if (self->fdel == nullptr) {
                PyObject* name = PyObject_GetAttr(
                    self->fget,
                    ptr(template_string<"__name__">())
                );
                if (name == nullptr) {
                    return nullptr;
                }
                PyErr_Format(
                    PyExc_AttributeError,
                    "property '%U' of %R object has no deleter",
                    name,
                    self->cls
                );
                Py_DECREF(name);
                return nullptr;
            }
            return PyObject_CallOneArg(self->fdel, obj);
        }

        /// TODO: @setter/@deleter decorators?  

        static PyObject* __repr__(Property* self) noexcept {
            try {
                std::string str =
                    "<property(" + repr(reinterpret_borrow<Object>(
                        reinterpret_cast<PyObject*>(self->fget))
                    ) + ")>";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        /// TODO: these may need to be properties, so that assigning to a
        /// property's setter/deleter will convert the object into a C++ function

        /// TODO: properties must expose a __wrapped__ member that points to the
        /// getter.

        static PyMemberDef members[];

    };

    PyMemberDef Property::members[] = {
        {
            "fget",
            Py_T_OBJECT_EX,
            offsetof(Property, fget),
            Py_READONLY,
            nullptr
        },
        {
            "fset",
            Py_T_OBJECT_EX,
            offsetof(Property, fset),
            0,
            nullptr
        },
        {
            "fdel",
            Py_T_OBJECT_EX,
            offsetof(Property, fdel),
            0,
            nullptr
        },
        {nullptr}
    };

    PyTypeObject Property::__type__ = {
        .ob_base = PyVarObject_HEAD_INIT(NULL, 0)
        .tp_name = typeid(Property).name(),
        .tp_basicsize = sizeof(Property),
        .tp_itemsize = 0,
        .tp_dealloc = reinterpret_cast<destructor>(&Property::__dealloc__),
        .tp_repr = reinterpret_cast<reprfunc>(&Property::__repr__),
        .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_DISALLOW_INSTANTIATION,
        .tp_doc = PyDoc_STR(
R"doc(A descriptor that binds a C++ function as a property getter of a Python
class.

Notes
-----
This descriptor can only be instantiated by applying the `@property` decorator
of a bertrand function to a Python type.

Note that each template instantiation exposes a unique descriptor type, which
mirrors C++ semantics and enables structural typing via `isinstance()` and
`issubclass()`.)doc"
        ),
        .tp_members = Property::members,
        .tp_descr_get = reinterpret_cast<descrgetfunc>(&Property::__get__),
        .tp_descr_set = reinterpret_cast<descrsetfunc>(&Property::__set__),
    };

    /* The Python `bertrand.Function[]` template interface type, which holds all
    instantiations, each of which inherit from this class, and allows for CTAD-like
    construction via the `__new__()` operator.  Has no interface otherwise, requiring
    the user to manually instantiate it as if it were a C++ template. */
    struct FunctionTemplates : PyObject {

        /// TODO: this HAS to be a heap type because it is an instance of the metaclass,
        /// and therefore always has mutable state.
        /// -> Maybe when writing bindings, this is just given as a function to the
        /// binding generator, and it would be responsible for implementing the
        /// template interface's CTAD constructor, and it would use Python-style
        /// argument annotations just like any other function.

        /// TODO: Okay, the way to do this is to have the bindings automatically
        /// populate tp_new with an overloadable function, and then the user can
        /// register overloads directly from Python.  The function you supply to the
        /// binding helper would be inserted as the base case, which defaults to
        /// raising a TypeError if the user tries to instantiate the template.  If
        /// that is the case, then I might be able to automatically register overloads
        /// as each type is instantiated, in a way that doesn't cause errors if the
        /// overload conflicts with an existing one.
        /// -> If I implement that using argument annotations, then this gets
        /// substantially simpler as well, since I don't need to extract the arguments
        /// manually.

        /// TODO: remember to set tp_vectorcall to this method, so I don't need to
        /// implement real __new__/__init__ constructors.
        static PyObject* __new__(
            FunctionTemplates* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) {
            try {
                size_t nargs = PyVectorcall_NARGS(nargsf);
                size_t kwcount = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
                if (nargs != 1) {
                    throw TypeError(
                        "expected a single, positional-only argument, but "
                        "received " + std::to_string(nargs)
                    );
                }
                PyObject* func = args[0];
                Object name = reinterpret_steal<Object>(nullptr);
                Object doc = reinterpret_steal<Object>(nullptr);
                if (kwcount) {
                    for (size_t i = 0; i < kwcount; ++i) {
                        PyObject* key = PyTuple_GET_ITEM(kwnames, i);
                        int is_name = PyObject_RichCompareBool(
                            key,
                            ptr(template_string<"name">()),
                            Py_EQ
                        );
                        if (is_name < 0) {
                            Exception::from_python();
                        } else if (is_name) {
                            name = reinterpret_borrow<Object>(args[nargs + i]);
                            if (!PyUnicode_Check(ptr(name))) {
                                throw TypeError(
                                    "expected 'name' to be a string, but received " +
                                    repr(name)
                                );
                            }
                        }
                        int is_doc = PyObject_RichCompareBool(
                            key,
                            ptr(template_string<"doc">()),
                            Py_EQ
                        );
                        if (is_doc < 0) {
                            Exception::from_python();
                        } else if (is_doc) {
                            doc = reinterpret_borrow<Object>(args[nargs + i]);
                            if (!PyUnicode_Check(ptr(doc))) {
                                throw TypeError(
                                    "expected 'doc' to be a string, but received " +
                                    repr(doc)
                                );
                            }
                        }
                        if (!is_name && !is_doc) {
                            throw TypeError(
                                "unexpected keyword argument '" +
                                repr(reinterpret_borrow<Object>(key)) + "'"
                            );
                        }
                    }
                }

                // inspect the input function and subscript the template interface to
                // get the correct specialization
                impl::Inspect signature = {
                    func,
                    impl::fnv1a_seed,
                    impl::fnv1a_prime
                };
                Object specialization = reinterpret_steal<Object>(
                    PyObject_GetItem(
                        self,
                        ptr(signature.template_key())
                    )
                );
                if (specialization.is(nullptr)) {
                    Exception::from_python();
                }

                // if the parameter list contains unions, then we need to default-
                // initialize the specialization and then register separate overloads
                // for each path through the parameter list.  Note that if the function
                // is the only argument and already exactly matches the deduced type,
                // then we can just return it directly to avoid unnecessary nesting.
                Object result = reinterpret_steal<Object>(nullptr);
                if (signature.size() > 1) {
                    if (!kwcount) {
                        if (specialization.is(
                            reinterpret_cast<PyObject*>(Py_TYPE(func))
                        )) {
                            return release(specialization);
                        }
                        result = reinterpret_steal<Object>(PyObject_CallNoArgs(
                            ptr(specialization)
                        ));
                    } else if (name.is(nullptr)) {
                        PyObject* args[] = {
                            nullptr,
                            ptr(doc),
                        };
                        result = reinterpret_steal<Object>(PyObject_Vectorcall(
                            ptr(specialization),
                            args,
                            kwcount | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            kwnames
                        ));
                    } else if (doc.is(nullptr)) {
                        PyObject* args[] = {
                            nullptr,
                            ptr(name),
                        };
                        result = reinterpret_steal<Object>(PyObject_Vectorcall(
                            ptr(specialization),
                            args,
                            kwcount | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            kwnames
                        ));
                    } else {
                        PyObject* args[] = {
                            nullptr,
                            ptr(name),
                            ptr(doc),
                        };
                        result = reinterpret_steal<Object>(PyObject_Vectorcall(
                            ptr(specialization),
                            args,
                            kwcount | PY_VECTORCALL_ARGUMENTS_OFFSET,
                            kwnames
                        ));
                    }
                    if (result.is(nullptr)) {
                        Exception::from_python();
                    }
                    Object rc = reinterpret_steal<Object>(PyObject_CallMethodOneArg(
                        ptr(result),
                        ptr(impl::template_string<"overload">()),
                        func
                    ));
                    if (rc.is(nullptr)) {
                        Exception::from_python();
                    }
                    return release(result);
                }

                // otherwise, we can initialize the specialization directly, which
                // captures the function and uses it as the base case
                if (!kwcount) {
                    if (specialization.is(
                        reinterpret_cast<PyObject*>(Py_TYPE(func))
                    )) {
                        return release(specialization);
                    }
                    result = reinterpret_steal<Object>(PyObject_CallOneArg(
                        ptr(specialization),
                        func
                    ));
                } else if (name.is(nullptr)) {
                    PyObject* args[] = {
                        nullptr,
                        func,
                        ptr(doc),
                    };
                    result = reinterpret_steal<Object>(PyObject_Vectorcall(
                        ptr(specialization),
                        args,
                        kwcount + 1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        kwnames
                    ));
                } else if (doc.is(nullptr)) {
                    PyObject* args[] = {
                        nullptr,
                        func,
                        ptr(name),
                    };
                    result = reinterpret_steal<Object>(PyObject_Vectorcall(
                        ptr(specialization),
                        args,
                        kwcount + 1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        kwnames
                    ));
                } else {
                    PyObject* args[] = {
                        nullptr,
                        func,
                        ptr(name),
                        ptr(doc),
                    };
                    result = reinterpret_steal<Object>(PyObject_Vectorcall(
                        ptr(specialization),
                        args,
                        kwcount + 1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                        kwnames
                    ));
                }
                if (result.is(nullptr)) {
                    Exception::from_python();
                }
                return release(result);

            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    };

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


template <typename F>
struct Interface<Function<F>> : impl::FunctionTag {

    /* The normalized function pointer type for this specialization. */
    using Signature = impl::Signature<F>::type;

    /* The type of the function's `self` argument, or void if it is not a member
    function. */
    using Self = impl::Signature<F>::Self;

    /* A tuple holding the function's default values, which are inferred from the input
    signature. */
    using Defaults = impl::Signature<F>::Defaults;

    /* A trie-based data structure describing dynamic overloads for a function
    object. */
    using Overloads = impl::Signature<F>::Overloads;

    /* The function's return type. */
    using Return = impl::Signature<F>::Return;

    /* Instantiate a new function type with the same arguments, but a different return
    type. */
    template <typename R> requires (std::convertible_to<R, Object>)
    using with_return =
        Function<typename impl::Signature<F>::template with_return<R>::type>;

    /* Instantiate a new function type with the same return type and arguments, but
    bound to a particular type. */
    template <typename C>
        requires (
            std::convertible_to<C, Object> &&
            impl::Signature<F>::template can_make_member<C>
        )
    using with_self =
        Function<typename impl::Signature<F>::template with_self<C>::type>;

    /* Instantiate a new function type with the same return type, but different
    arguments. */
    template <typename... A>
        requires (
            sizeof...(A) <= (64 - impl::Signature<F>::has_self) &&
            impl::Arguments<A...>::args_are_convertible_to_python &&
            impl::Arguments<A...>::proper_argument_order &&
            impl::Arguments<A...>::no_duplicate_arguments &&
            impl::Arguments<A...>::no_required_after_default
        )
    using with_args =
        Function<typename impl::Signature<F>::template with_args<A...>::type>;

    /* Check whether a target function can be registered as a valid overload of this
    function type.  Such a function must minimally account for all the arguments in
    this function signature (which may be bound to subclasses), and list a return
    type that can be converted to this function's return type.  If the function accepts
    variadic positional or keyword arguments, then overloads may include any number of
    additional parameters in their stead, as long as all of those parameters are
    convertible to the variadic type. */
    template <typename Func>
    static constexpr bool compatible = false;

    template <typename Func>
        requires (impl::Signature<std::remove_cvref_t<Func>>::enable)
    static constexpr bool compatible<Func> =
        []<size_t... Is>(std::index_sequence<Is...>) {
            return impl::Signature<F>::template compatible<
                typename impl::Signature<std::remove_cvref_t<Func>>::Return,
                typename impl::Signature<std::remove_cvref_t<Func>>::template at<Is>...
            >;
        }(std::make_index_sequence<impl::Signature<std::remove_cvref_t<Func>>::n>{});

    template <typename Func>
        requires (
            !impl::Signature<std::remove_cvref_t<Func>>::enable &&
            impl::inherits<Func, impl::FunctionTag>
        )
    static constexpr bool compatible<Func> = compatible<
        typename std::remove_reference_t<Func>::Signature
    >;

    template <typename Func>
        requires (
            !impl::Signature<Func>::enable &&
            !impl::inherits<Func, impl::FunctionTag> &&
            impl::has_call_operator<Func>
        )
    static constexpr bool compatible<Func> = 
        impl::Signature<decltype(&std::remove_reference_t<Func>::operator())>::enable &&
        compatible<
            typename impl::Signature<decltype(&std::remove_reference_t<Func>::operator())>::
            template with_self<void>::type
        >;

    /* Check whether this function type can be used to invoke an external C++ function.
    This is identical to a `std::is_invocable_r_v<Func, ...>` check against this
    function's return and argument types.  Note that member functions expect a `self`
    parameter to be listed first, following Python style. */
    template <typename Func>
    static constexpr bool invocable = impl::Signature<F>::template invocable<Func>;

    /* Check whether the function can be called with the given arguments, after
    accounting for optional/variadic/keyword arguments, etc. */
    template <typename... Args>
    static constexpr bool bind = impl::Signature<F>::template Bind<Args...>::enable;

    /* The total number of arguments that the function accepts, not counting `self`. */
    static constexpr size_t n = impl::Signature<F>::n;

    /* The total number of positional-only arguments that the function accepts. */
    static constexpr size_t n_posonly = impl::Signature<F>::n_posonly;

    /* The total number of positional arguments that the function accepts, counting
    both positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_pos = impl::Signature<F>::n_pos;

    /* The total number of keyword arguments that the function accepts, counting
    both positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_kw = impl::Signature<F>::n_kw;

    /* The total number of keyword-only arguments that the function accepts. */
    static constexpr size_t n_kwonly = impl::Signature<F>::n_kwonly;

    /* The total number of optional arguments that are present in the function
    signature, including both positional and keyword arguments. */
    static constexpr size_t n_opt = impl::Signature<F>::n_opt;

    /* The total number of optional positional-only arguments that the function
    accepts. */
    static constexpr size_t n_opt_posonly = impl::Signature<F>::n_opt_posonly;

    /* The total number of optional positional arguments that the function accepts,
    counting both positional-only and positional-or-keyword arguments, but not
    keyword-only or variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_opt_pos = impl::Signature<F>::n_opt_pos;

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

    /* Check if the function accepts any positional-only arguments. */
    static constexpr bool has_posonly = impl::Signature<F>::has_posonly;

    /* Check if the function accepts any positional arguments, counting both
    positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_pos = impl::Signature<F>::has_pos;

    /* Check if the function accepts any keyword arguments, counting both
    positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_kw = impl::Signature<F>::has_kw;

    /* Check if the function accepts any keyword-only arguments. */
    static constexpr bool has_kwonly = impl::Signature<F>::has_kwonly;

    /* Check if the function accepts at least one optional argument. */
    static constexpr bool has_opt = impl::Signature<F>::has_opt;

    /* Check if the function accepts at least one optional positional-only argument. */
    static constexpr bool has_opt_posonly = impl::Signature<F>::has_opt_posonly;

    /* Check if the function accepts at least one optional positional argument.  This
    will match either positional-or-keyword or positional-only arguments. */
    static constexpr bool has_opt_pos = impl::Signature<F>::has_opt_pos;

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

    /* Find the index of the first optional positional-only argument in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_posonly_idx = impl::Signature<F>::opt_posonly_index;

    /* Find the index of the first optional positional argument in the function
    signature.  This will match either a positional-or-keyword argument or a
    positional-only argument.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_pos_idx = impl::Signature<F>::opt_pos_index;

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

    /* Get the (annotated) type of the argument at index I of the function's
    signature. */
    template <size_t I> requires (I < n)
    using at = impl::Signature<F>::template at<I>;

    /* A bitmask of all the required arguments needed to call this function.  This is
    used during argument validation to quickly determine if the parameter list is
    satisfied when keyword are provided out of order, etc. */
    static constexpr uint64_t required = impl::Signature<F>::required;

    /* An FNV-1a seed that was found to perfectly hash the function's keyword argument
    names. */
    static constexpr size_t seed = impl::Signature<F>::seed;

    /* The FNV-1a prime number that was found to perfectly hash the function's keyword
    argument names. */
    static constexpr size_t prime = impl::Signature<F>::prime;

    /* Hash a string according to the seed and prime that were found at compile time to
    perfectly hash this function's keyword arguments. */
    [[nodiscard]] static constexpr size_t hash(const char* str) noexcept {
        return impl::Signature<F>::hash(str);
    }
    [[nodiscard]] static constexpr size_t hash(std::string_view str) noexcept {
        return impl::Signature<F>::hash(str);
    }
    [[nodiscard]] static constexpr size_t hash(const std::string& str) noexcept {
        return impl::Signature<F>::hash(str);
    }

    /* Call an external C++ function that matches the target signature using
    Python-style arguments.  The default values (if any) must be provided as an
    initializer list immediately before the function to be invoked, or constructed
    elsewhere and passed by reference.  This helper has no overhead over a traditional
    C++ function call, disregarding any logic necessary to construct default values. */
    template <typename Func, typename... Args> requires (invocable<Func> && bind<Args...>)
    static Return call(const Defaults& defaults, Func&& func, Args&&... args) {
        return typename impl::Signature<F>::template Bind<Args...>{}(
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
    template <impl::inherits<Object> R = Object, typename... Args>
        requires (!std::is_reference_v<R> && bind<Args...>)
    static Return call(PyObject* func, Args&&... args) {
        PyObject* result = typename impl::Signature<F>::template Bind<Args...>{}(
            func,
            std::forward<Args>(args)...
        );
        if constexpr (std::is_void_v<Return>) {
            Py_DECREF(result);
        } else {
            return reinterpret_steal<R>(result);
        }
    }

    /* Register an overload for this function from C++. */
    template <typename Self, typename Func>
        requires (
            !std::is_const_v<std::remove_reference_t<Self>> &&
            compatible<Func>
        )
    void overload(this Self&& self, const Function<Func>& func);

    /* Attach the function as a bound method of a Python type. */
    template <typename T>
    void method(this const auto& self, Type<T>& type);

    template <typename T>
    void classmethod(this const auto& self, Type<T>& type);

    template <typename T>
    void staticmethod(this const auto& self, Type<T>& type);

    template <typename T>
    void property(
        this const auto& self,
        Type<T>& type,
        /* setter */,
        /* deleter */
    );

    /// TODO: when getting and setting these properties, do I need to use Attr
    /// proxies for consistency?

    __declspec(property(get=_get_name, put=_set_name)) std::string __name__;
    [[nodiscard]] std::string _get_name(this const auto& self);
    void _set_name(this auto& self, const std::string& name);

    __declspec(property(get=_get_doc, put=_set_doc)) std::string __doc__;
    [[nodiscard]] std::string _get_doc(this const auto& self);
    void _set_doc(this auto& self, const std::string& doc);

    /// TODO: __defaults__ should return a std::tuple of default values, as they are
    /// given in the signature.

    __declspec(property(get=_get_defaults, put=_set_defaults))
        std::optional<Tuple<Object>> __defaults__;
    [[nodiscard]] std::optional<Tuple<Object>> _get_defaults(this const auto& self);
    void _set_defaults(this auto& self, const Tuple<Object>& defaults);

    /// TODO: This should return a std::tuple of Python type annotations for each
    /// argument.

    __declspec(property(get=_get_annotations, put=_set_annotations))
        std::optional<Dict<Str, Object>> __annotations__;
    [[nodiscard]] std::optional<Dict<Str, Object>> _get_annotations(this const auto& self);
    void _set_annotations(this auto& self, const Dict<Str, Object>& annotations);

    /// TODO: __signature__, which returns a proper Python `inspect.Signature` object.

};
template <typename F>
struct Interface<Type<Function<F>>> {

    /* The normalized function pointer type for this specialization. */
    using Signature = Interface<Function<F>>::Signature;

    /* The type of the function's `self` argument, or void if it is not a member
    function. */
    using Self = Interface<Function<F>>::Self;

    /* A tuple holding the function's default values, which are inferred from the input
    signature and stored as a `std::tuple`. */
    using Defaults = Interface<Function<F>>::Defaults;

    /* A trie-based data structure describing dynamic overloads for a function
    object. */
    using Overloads = Interface<Function<F>>::Overloads;

    /* The function's return type. */
    using Return = Interface<Function<F>>::Return;

    /* Instantiate a new function type with the same arguments, but a different return
    type. */
    template <typename R> requires (std::convertible_to<R, Object>)
    using with_return = Interface<Function<F>>::template with_return<R>;

    /* Instantiate a new function type with the same return type and arguments, but
    bound to a particular type. */
    template <typename C>
        requires (
            std::convertible_to<C, Object> &&
            impl::Signature<F>::template can_make_member<C>
        )
    using with_self = Interface<Function<F>>::template with_self<C>;

    /* Instantiate a new function type with the same return type, but different
    arguments. */
    template <typename... A>
        requires (
            sizeof...(A) <= (64 - impl::Signature<F>::has_self) &&
            impl::Arguments<A...>::args_are_convertible_to_python &&
            impl::Arguments<A...>::proper_argument_order &&
            impl::Arguments<A...>::no_duplicate_arguments &&
            impl::Arguments<A...>::no_required_after_default
        )
    using with_args = Interface<Function<F>>::template with_args<A...>;

    /* Check whether a target function can be registered as a valid overload of this
    function type.  Such a function must minimally account for all the arguments in
    this function signature (which may be bound to subclasses), and list a return
    type that can be converted to this function's return type.  If the function accepts
    variadic positional or keyword arguments, then overloads may include any number of
    additional parameters in their stead, as long as all of those parameters are
    convertible to the variadic type. */
    template <typename Func>
    static constexpr bool compatible = Interface<Function<F>>::template compatible<Func>;

    /* Check whether this function type can be used to invoke an external C++ function.
    This is identical to a `std::is_invocable_r_v<Func, ...>` check against this
    function's return and argument types.  Note that member functions expect a `self`
    parameter to be listed first, following Python style. */
    template <typename Func>
    static constexpr bool invocable = Interface<Function<F>>::template invocable<Func>;

    /* Check whether the function can be called with the given arguments, after
    accounting for optional/variadic/keyword arguments, etc. */
    template <typename... Args>
    static constexpr bool bind = Interface<Function<F>>::template bind<Args...>;

    /* The total number of arguments that the function accepts, not counting `self`. */
    static constexpr size_t n = Interface<Function<F>>::n;

    /* The total number of positional-only arguments that the function accepts. */
    static constexpr size_t n_posonly = Interface<Function<F>>::n_posonly;

    /* The total number of positional arguments that the function accepts, counting
    both positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_pos = Interface<Function<F>>::n_pos;

    /* The total number of keyword arguments that the function accepts, counting
    both positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_kw = Interface<Function<F>>::n_kw;

    /* The total number of keyword-only arguments that the function accepts. */
    static constexpr size_t n_kwonly = Interface<Function<F>>::n_kwonly;

    /* The total number of optional arguments that are present in the function
    signature, including both positional and keyword arguments. */
    static constexpr size_t n_opt = Interface<Function<F>>::n_opt;

    /* The total number of optional positional-only arguments that the function
    accepts. */
    static constexpr size_t n_opt_posonly = Interface<Function<F>>::n_opt_posonly;

    /* The total number of optional positional arguments that the function accepts,
    counting both positional-only and positional-or-keyword arguments, but not
    keyword-only or variadic positional or keyword arguments, or `self`. */
    static constexpr size_t n_opt_pos = Interface<Function<F>>::n_opt_pos;

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

    /* Check if the function accepts any positional-only arguments. */
    static constexpr bool has_posonly = Interface<Function<F>>::has_posonly;

    /* Check if the function accepts any positional arguments, counting both
    positional-or-keyword and positional-only arguments, but not keyword-only,
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_pos = Interface<Function<F>>::has_pos;

    /* Check if the function accepts any keyword arguments, counting both
    positional-or-keyword and keyword-only arguments, but not positional-only or
    variadic positional or keyword arguments, or `self`. */
    static constexpr bool has_kw = Interface<Function<F>>::has_kw;

    /* Check if the function accepts any keyword-only arguments. */
    static constexpr bool has_kwonly = Interface<Function<F>>::has_kwonly;

    /* Check if the function accepts at least one optional argument. */
    static constexpr bool has_opt = Interface<Function<F>>::has_opt;

    /* Check if the function accepts at least one optional positional-only argument. */
    static constexpr bool has_opt_posonly = Interface<Function<F>>::has_opt_posonly;

    /* Check if the function accepts at least one optional positional argument.  This
    will match either positional-or-keyword or positional-only arguments. */
    static constexpr bool has_opt_pos = Interface<Function<F>>::has_opt_pos;

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

    /* Find the index of the first optional positional-only argument in the function
    signature.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_posonly_idx = Interface<Function<F>>::opt_posonly_index;

    /* Find the index of the first optional positional argument in the function
    signature.  This will match either a positional-or-keyword argument or a
    positional-only argument.  If no such argument is present, this will return `n`. */
    static constexpr size_t opt_pos_idx = Interface<Function<F>>::opt_pos_index;

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

    /* A bitmask of all the required arguments needed to call this function.  This is
    used during argument validation to quickly determine if the parameter list is
    satisfied when keyword are provided out of order, etc. */
    static constexpr uint64_t required = Interface<Function<F>>::required;

    /* An FNV-1a seed that was found to perfectly hash the function's keyword argument
    names. */
    static constexpr size_t seed = Interface<Function<F>>::seed;

    /* The FNV-1a prime number that was found to perfectly hash the function's keyword
    argument names. */
    static constexpr size_t prime = Interface<Function<F>>::prime;

    /* Hash a string according to the seed and prime that were found at compile time to
    perfectly hash this function's keyword arguments. */
    [[nodiscard]] static constexpr size_t hash(const char* str) noexcept {
        return impl::Signature<F>::hash(str);
    }
    [[nodiscard]] static constexpr size_t hash(std::string_view str) noexcept {
        return impl::Signature<F>::hash(str);
    }
    [[nodiscard]] static constexpr size_t hash(const std::string& str) noexcept {
        return impl::Signature<F>::hash(str);
    }

    /* Call an external C++ function that matches the target signature using
    Python-style arguments.  The default values (if any) must be provided as an
    initializer list immediately before the function to be invoked, or constructed
    elsewhere and passed by reference.  This helper has no overhead over a traditional
    C++ function call, disregarding any logic necessary to construct default values. */
    template <typename Func, typename... Args> requires (invocable<Func> && bind<Args...>)
    static Return call(const Defaults& defaults, Func&& func, Args&&... args) {
        return Interface<Function<F>>::call(
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
    template <impl::inherits<Object> R = Object, typename... Args>
        requires (!std::is_reference_v<R> && bind<Args...>)
    static Return call(PyObject* func, Args&&... args) {
        return Interface<Function<F>>::template call<R>(
            func,
            std::forward<Args>(args)...
        );
    }

    /* Register an overload for this function. */
    template <impl::inherits<Interface<Function<F>>> Self, typename Func>
        requires (!std::is_const_v<std::remove_reference_t<Self>> && compatible<Func>)
    void overload(Self&& self, const Function<Func>& func) {
        std::forward<Self>(self).overload(func);
    }

    /* Attach the function as a bound method of a Python type. */
    template <impl::inherits<Interface<Function<F>>> Self, typename T>
    void method(const Self& self, Type<T>& type) {
        std::forward<Self>(self).method(type);
    }

    template <impl::inherits<Interface<Function<F>>> Self, typename T>
    void classmethod(const Self& self, Type<T>& type) {
        std::forward<Self>(self).classmethod(type);
    }

    template <impl::inherits<Interface<Function<F>>> Self, typename T>
    void staticmethod(const Self& self, Type<T>& type) {
        std::forward<Self>(self).staticmethod(type);
    }

    template <impl::inherits<Interface<Function<F>>> Self, typename T>
    void property(const Self& self, Type<T>& type, /* setter */, /* deleter */) {
        std::forward<Self>(self).property(type);
    }

    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::string __name__(const Self& self) {
        return self.__name__;
    }

    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::string __doc__(const Self& self) {
        return self.__doc__;
    }

    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<Tuple<Object>> __defaults__(const Self& self);

    template <impl::inherits<Interface> Self>
    [[nodiscard]] static std::optional<Dict<Str, Object>> __annotations__(const Self& self);

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

    /* Non-member function type. */
    template <typename Sig>
    struct PyFunction : def<PyFunction<Sig>, Function>, PyObject {
        static constexpr StaticStr __doc__ =
R"doc(A Python wrapper around a C++ function.

Notes
-----
This type is not directly instantiable from Python.  Instead, it can only be
accessed through the `bertrand.Function` template interface, which can be
navigated by subscripting the interface according to a possible function
signature.

Examples
--------
>>> from bertrand import Function
>>> Function[::int, "x": int, "y": int]
<class 'py::Function<py::Int(*)(py::Arg<"x", py::Int>, py::Arg<"y", py::Int>)>'>
>>> Function[::None, "*objects": object, "sep": str: ..., "end": str: ..., "file": object: ..., "flush": bool: ...]
<class 'py::Function<void(*)(py::Arg<"objects", py::Object>::args, py::Arg<"sep", py::Str>::opt, py::Arg<"end", py::Str>::opt, py::Arg<"file", py::Object>::opt, py::Arg<"flush", py::Bool>::opt)>'>
>>> Function[list[object]::None, "*", "key": object: ..., "reverse": bool: ...]
<class 'py::Function<void(py::List<py::Object>::*)(py::Arg<"key", py::Object>::kw::opt, py::Arg<"reverse", py::Bool>::kw::opt)>'>
>>> Function[type[bytes]::bytes, "string": str, "/"]
<class 'py::Function<py::Bytes(Type<py::Bytes>::*)(py::Arg<"string", py::Str>::pos)>'>

Each of these accessors will resolve to a unique Python type that wraps a
specific C++ function signature.

The 2nd example shows the template signature of the built-in `print()`
function, which returns void and accepts variadic positional arguments of any
type, followed by keyword arguments of various types, all of which are optional
(indicated by the trailing `...` syntax).

The 3rd example represents a bound member function corresponding to the
built-in `list.sort()` method, which accepts two optional keyword-only
arguments, where the list can contain any type.  The `*` delimiter works
just like a standard Python function declaration in this case, with equivalent
semantics.  The type of the bound `self` parameter is given on the left side of
the `list[object]::None` return type, which can be thought of similar to a C++
`::` scope accessor.  The type on the right side is the method's normal return
type, which in this case is `None`.

The 4th example represents a class method corresponding to the built-in
`bytes.fromhex()` method, which accepts a single, required, positional-only
argument of type `str`.  The `/` delimiter is used to indicate positional-only
arguments similar to `*`.  The type of the `self` parameter in this case is
given as a subscription of `type[]`, which indicates that the bound `self`
parameter is a type object, and thus the method is a class method.)doc";

        vectorcallfunc call = reinterpret_cast<vectorcallfunc>(__call__);
        PyObject* pyfunc = nullptr;
        PyObject* pysignature = nullptr;
        Sig::Defaults defaults;
        std::function<typename Sig::to_value::type> func;
        Sig::Overloads overloads;
        PyObject* name = nullptr;
        PyObject* docstring = nullptr;

        /* Exposes a C++ function to Python */
        explicit PyFunction(
            Object&& name,
            Object&& docstring,
            std::function<typename Sig::to_value::type>&& func,
            Sig::Defaults&& defaults
        ) : defaults(std::move(defaults)), func(std::move(func)),
            name(release(name)), docstring(release(docstring))
        {}

        /* Exposes a Python function to C++ by generating a capturing lambda wrapper,
        after a quick signature validation.  The function must exactly match the
        enclosing signature, including argument names, types, and
        posonly/kwonly/optional/variadic qualifiers.  If the function lists union types
        for one or more arguments, then only one path through the parameter list needs
        to match for the conversion to succeed.  This is called by the narrowing cast
        operator when converting a dynamic object to this function type. */
        explicit PyFunction(
            PyObject* pyfunc,
            PyObject* name = nullptr,
            PyObject* docstring = nullptr,
            impl::Inspect* signature = nullptr
        ) :
            pyfunc(pyfunc),
            defaults([](PyObject* pyfunc, impl::Inspect* signature) {
                if (signature) {
                    return validate_signature(pyfunc, *signature);
                } else {
                    impl::Inspect signature = {pyfunc, Sig::seed, Sig::prime};
                    return validate_signature(pyfunc, signature);
                }
            }(pyfunc, signature)),
            func(Sig::capture(pyfunc))
        {
            this->name = name ? Py_NewRef(name) : PyObject_GetAttr(
                name,
                ptr(impl::template_string<"__name__">())
            );
            if (this->name == nullptr) {
                Exception::from_python();
            }
            this->docstring = docstring ? Py_NewRef(docstring) : PyObject_GetAttr(
                docstring,
                ptr(impl::template_string<"__doc__">())
            );
            if (this->docstring == nullptr) {
                Py_DECREF(this->name);
                Exception::from_python();
            }
            Py_INCREF(this->pyfunc);
        }

        ~PyFunction() noexcept {
            Py_XDECREF(pyfunc);
            Py_XDECREF(pysignature);
            Py_XDECREF(name);
            Py_XDECREF(docstring);
        }

        static void __dealloc__(PyFunction* self) noexcept {
            PyObject_GC_UnTrack(self);
            self->~PyFunction();
            Py_TYPE(self)->tp_free(self);
        }

        /* Python-level allocation function.  Initializes pointers to null, but
        otherwise does nothing in order to allow for future re-initialization.  If the
        initializer function is already an instance of this type, then it is returned
        as-is so as to avoid unnecessary nesting. */
        static PyObject* __new__(
            PyTypeObject* cls,
            PyObject* args,
            PyObject* kwds
        ) noexcept {
            PyFunction* self = reinterpret_cast<PyFunction*>(cls->tp_alloc(cls, 0));
            if (self == nullptr) {
                return nullptr;
            }
            self->call = reinterpret_cast<vectorcallfunc>(__call__);
            self->pyfunc = nullptr;
            self->pysignature = nullptr;
            self->name = nullptr;
            self->docstring = nullptr;
            return reinterpret_cast<PyObject*>(self);
        }

        /* Python-level constructor.  This is called by the `@bertrand` decorator once
        this template has been deduced from a valid signature.  Additionally, since
        the `__new__()` method bypasses this constructor when the initializer is
        already an instance of this type, and C++ narrowing conversions use a dedicated
        constructor, this method will only be called for Python functions that are
        explicitly wrapped directly from Python.

        In this case, we need special logic to properly handle union types in the
        supplied signature, which must be encoded into the overload trie to allow for
        proper dispatching:

            1.  If one member of the union is a supertype of all the others, then the
                base function will use that type, and register the others as separate
                overloads that resolve to the same function.
            2.  Otherwise, we traverse each type's MRO to find the first common
                ancestor, and use that for the base function signature, with all of the
                original paths as overloads.  This eventually terminates at the dynamic
                `object` type, which is the ultimate base of all Python types.
            3.  If the common type that describes all elements of the union is not
                itself an element of the union, then we generate an implicit base
                function that raises a `TypeError` when called, and register each
                member of the union as a separate overload that corrects this.  That
                way, when the function is called, all members of the union will be
                matched against the observed arguments, falling back to the error case
                if none of them match.
            4.  In order to retain accurate static analysis, the resulting function
                will directly reference the `inspect.Signature` object of the
                initializer, even if we synthesized a new base function from a
                supertype.
         */
        static int __init__(
            PyFunction* self,
            PyObject* args,
            PyObject* kwargs
        ) noexcept {
            /// TODO: if no positional arguments are provided, generate a default base
            /// function that immediately raises a TypeError.  In this case, the name
            /// and docstring can be passed in as keyword arguments, otherwise they
            /// are inferred from the function itself.
            /// -> Actually what I should do is allow the keyword arguments to be
            /// supplied at all times, in order to allow for binding lambdas and other
            /// function objects in Python.

            try {
                size_t nargs = PyTuple_GET_SIZE(args);
                if (nargs > 1) {
                    throw TypeError(
                        "expected at most one positional argument, but received " +
                        std::to_string(nargs)
                    );
                }
                Object name = reinterpret_steal<Object>(nullptr);
                Object doc = reinterpret_steal<Object>(nullptr);
                if (kwargs) {
                    name = reinterpret_steal<Object>(PyDict_GetItem(
                        kwargs,
                        ptr(impl::template_string<"name">())
                    ));
                    if (!name.is(nullptr) && !PyUnicode_Check(ptr(name))) {
                        throw TypeError(
                            "expected 'name' to be a string, not: " + repr(name)
                        );
                    }
                    doc = reinterpret_steal<Object>(PyDict_GetItem(
                        kwargs,
                        ptr(impl::template_string<"doc">())
                    ));
                    if (!doc.is(nullptr) && !PyUnicode_Check(ptr(doc))) {
                        throw TypeError(
                            "expected 'doc' to be a string, not: " + repr(doc)
                        );
                    }
                    Py_ssize_t observed = name.is(nullptr) + doc.is(nullptr);
                    if (observed != PyDict_Size(kwargs)) {
                        throw TypeError(
                            "received unexpected keyword argument(s): " +
                            repr(reinterpret_borrow<Object>(kwargs))
                        );
                    }
                }

                if (nargs == 0) {
                    /// TODO: generate a default base function that raises a TypeError
                    /// when called, and forward to first constructor.
                }


                PyObject* func = PyTuple_GET_ITEM(args, 0);
                impl::Inspect signature = {func, Sig::seed, Sig::prime};

                // remember the original signature for the benefit of static analyzers,
                // documentation purposes, etc.
                new (self) PyFunction(
                    func,
                    nullptr,  /// TODO: name and docstring passed into constructor as kwargs
                    nullptr,
                    &signature
                );
                self->pysignature = release(signature.signature);
                PyObject_GC_Track(self);
                return 0;

            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        template <StaticStr ModName>
        static Type<Function> __export__(Module<ModName> bindings);
        static Type<Function> __import__();

        /* Register an overload from Python.  Accepts only a single argument, which
        must be a function or other callable object that can be passed to the
        `inspect.signature()` factory function.  That includes user-defined types with
        overloaded call operators, as long as the operator is properly annotated
        according to Python style, or the object provides a `__signature__` property
        that returns a valid `inspect.Signature` object.  This method can be used as a
        decorator from Python. */
        static PyObject* overload(PyFunction* self, PyObject* func) noexcept {
            try {
                Object obj = reinterpret_borrow<Object>(func);
                impl::Inspect signature = {func, Sig::seed, Sig::prime};
                for (PyObject* rtype : signature.returns()) {
                    Object type = reinterpret_borrow<Object>(rtype);
                    if (!issubclass<typename Sig::Return>(type)) {
                        std::string message =
                            "overload return type '" + repr(type) + "' is not a "
                            "subclass of " + repr(Type<typename Sig::Return>());
                        PyErr_SetString(PyExc_TypeError, message.c_str());
                        return nullptr;
                    }
                }
                auto it = signature.begin();
                auto end = signature.end();
                try {
                    while (it != end) {
                        self->overloads.insert(*it, obj);
                        ++it;
                    }
                } catch (...) {
                    auto it2 = signature.begin();
                    while (it2 != it) {
                        self->overloads.remove(*it2);
                        ++it2;
                    }
                    throw;
                }
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Manually clear the function's overload trie from Python. */
        static PyObject* clear(PyFunction* self) noexcept {
            try {
                self->overloads.clear();
                Py_RETURN_NONE;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Manually clear the function's overload cache from Python. */
        static PyObject* flush(PyFunction* self) noexcept {
            try {
                self->overloads.flush();
                Py_RETURN_NONE;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Attach a function to a type as an instance method descriptor.  Accepts the
        type to attach to, which can be provided by calling this method as a decorator
        from Python. */
        static PyObject* method(PyFunction* self, PyObject* cls) noexcept {
            try {
                if constexpr (Sig::n < 1 || !(
                    impl::ArgTraits<typename Sig::template at<0>>::pos() ||
                    impl::ArgTraits<typename Sig::template at<0>>::args()
                )) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "method() requires a function with at least one "
                        "positional argument"
                    );
                    return nullptr;
                } else {
                    using T = impl::ArgTraits<typename Sig::template at<0>>::type;
                    if (!PyType_Check(cls)) {
                        PyErr_Format(
                            PyExc_TypeError,
                            "expected a type object, not: %R",
                            cls
                        );
                        return nullptr;
                    }
                    if (!issubclass<T>(reinterpret_borrow<Object>(cls))) {
                        PyErr_Format(
                            PyExc_TypeError,
                            "class must be a must be a subclass of %R",
                            ptr(Type<T>())
                        );
                        return nullptr;
                    }
                    if (PyObject_HasAttr(cls, self->name)) {
                        PyErr_Format(
                            PyExc_TypeError,
                            "attribute '%U' already exists on type '%R'",
                            self->name,
                            cls
                        );
                        return nullptr;
                    }
                    if (PyObject_SetAttr(cls, self->name, self)) {
                        return nullptr;
                    }
                    return Py_NewRef(cls);
                }
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Attach a function to a type as a class method descriptor.  Accepts the type
        to attach to, which can be provided by calling this method as a decorator from
        Python. */
        static PyObject* classmethod(PyFunction* self, PyObject* cls) noexcept {
            try {
                if constexpr (Sig::n < 1 || !(
                    impl::ArgTraits<typename Sig::template at<0>>::pos() ||
                    impl::ArgTraits<typename Sig::template at<0>>::args()
                )) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "classmethod() requires a function with at least one "
                        "positional argument"
                    );
                    return nullptr;
                } else {
                    using T = impl::ArgTraits<typename Sig::template at<0>>::type;
                    if (!PyType_Check(cls)) {
                        PyErr_Format(
                            PyExc_TypeError,
                            "expected a type object, not: %R",
                            cls
                        );
                        return nullptr;
                    }
                    if (!issubclass<T>(reinterpret_borrow<Object>(cls))) {
                        PyErr_Format(
                            PyExc_TypeError,
                            "class must be a must be a subclass of %R",
                            ptr(Type<T>())
                        );
                        return nullptr;
                    }
                    if (PyObject_HasAttr(cls, self->name)) {
                        PyErr_Format(
                            PyExc_AttributeError,
                            "attribute '%U' already exists on type '%R'",
                            self->name,
                            cls
                        );
                        return nullptr;
                    }
                    using impl::ClassMethod;
                    ClassMethod* descr = reinterpret_cast<ClassMethod*>(
                        ClassMethod::__type__.tp_alloc(&ClassMethod::__type__, 0)
                    );
                    if (descr == nullptr) {
                        return nullptr;
                    }
                    try {
                        new (descr) ClassMethod(self, cls);
                    } catch (...) {
                        Py_DECREF(descr);
                        Exception::to_python();
                        return nullptr;
                    }
                    int rc = PyObject_SetAttr(cls, self->name, descr);
                    Py_DECREF(descr);
                    if (rc) {
                        return nullptr;
                    }
                    return Py_NewRef(cls);
                }
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Attach a function to a type as a static method descriptor.  Accepts the type
        to attach to, which can be provided by calling this method as a decorator from
        Python. */
        static PyObject* staticmethod(PyFunction* self, PyObject* cls) noexcept {
            if (!PyType_Check(cls)) {
                PyErr_Format(
                    PyExc_TypeError,
                    "expected a type object, not: %R",
                    cls
                );
                return nullptr;
            }
            if (PyObject_HasAttr(cls, self->name)) {
                PyErr_Format(
                    PyExc_AttributeError,
                    "attribute '%U' already exists on type '%R'",
                    self->name,
                    cls
                );
                return nullptr;
            }
            using impl::StaticMethod;
            StaticMethod* descr = reinterpret_cast<StaticMethod*>(
                StaticMethod::__type__.tp_alloc(&StaticMethod::__type__, 0)
            );
            if (descr == nullptr) {
                return nullptr;
            }
            try {
                new (descr) StaticMethod(self);
            } catch (...) {
                Py_DECREF(descr);
                Exception::to_python();
                return nullptr;
            }
            int rc = PyObject_SetAttr(cls, self->name, descr);
            Py_DECREF(descr);
            if (rc) {
                return nullptr;
            }
            return Py_NewRef(cls);
        }

        /* Attach a function to a type as a getset descriptor.  Accepts a type object
        to attach to, which can be provided by calling this method as a decorator from
        Python, as well as two keyword-only arguments for an optional setter and
        deleter.  The same getter/setter fields are available from the descriptor
        itself via traditional Python `@Type.property.setter` and
        `@Type.property.deleter` decorators. */
        static PyObject* property(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) noexcept {
            try {
                if constexpr (Sig::n < 1 || !(
                    impl::ArgTraits<typename Sig::template at<0>>::pos() ||
                    impl::ArgTraits<typename Sig::template at<0>>::args()
                )) {
                    PyErr_SetString(
                        PyExc_TypeError,
                        "property() requires a function with at least one "
                        "positional argument"
                    );
                    return nullptr;
                } else {
                    using T = impl::ArgTraits<typename Sig::template at<0>>::type;
                    size_t nargs = PyVectorcall_NARGS(nargsf);
                    PyObject* cls;
                    if (nargs == 0) {
                        PyErr_Format(
                            PyExc_TypeError,
                            "%U.property() requires a type object as the sole "
                            "positional argument",
                            self->name
                        );
                        return nullptr;
                    } else if (nargs == 1) {
                        cls = args[0];
                    } else {
                        PyErr_Format(
                            PyExc_TypeError,
                            "%U.property() takes exactly one positional "
                            "argument",
                            self->name
                        );
                        return nullptr;
                    }
                    if (!PyType_Check(cls)) {
                        PyErr_Format(
                            PyExc_TypeError,
                            "expected a type object, not: %R",
                            cls
                        );
                        return nullptr;
                    }
                    if (!issubclass<T>(reinterpret_borrow<Object>(cls))) {
                        PyErr_Format(
                            PyExc_TypeError,
                            "class must be a must be a subclass of %R",
                            ptr(Type<T>())
                        );
                        return nullptr;
                    }

                    PyObject* fset = nullptr;
                    PyObject* fdel = nullptr;
                    if (kwnames) {
                        Py_ssize_t kwcount = PyTuple_GET_SIZE(kwnames);
                        if (kwcount > 2) {
                            PyErr_SetString(
                                PyExc_TypeError,
                                "property() takes at most 2 keyword arguments"
                            );
                            return nullptr;
                        } else if (kwcount > 1) {
                            PyObject* key = PyTuple_GET_ITEM(kwnames, 0);
                            int rc = PyObject_RichCompareBool(
                                key,
                                ptr(impl::template_string<"setter">()),
                                Py_EQ
                            );
                            if (rc < 0) {
                                return nullptr;
                            } else if (rc) {
                                fset = args[1];
                            } else {
                                rc = PyObject_RichCompareBool(
                                    key,
                                    ptr(impl::template_string<"deleter">()),
                                    Py_EQ
                                );
                                if (rc < 0) {
                                    return nullptr;
                                } else if (rc) {
                                    fdel = args[1];
                                } else {
                                    PyErr_Format(
                                        PyExc_TypeError,
                                        "unexpected keyword argument '%U'",
                                        key
                                    );
                                    return nullptr;
                                }
                            }
                            key = PyTuple_GET_ITEM(kwnames, 1);
                            rc = PyObject_RichCompareBool(
                                key,
                                ptr(impl::template_string<"deleter">()),
                                Py_EQ
                            );
                            if (rc < 0) {
                                return nullptr;
                            } else if (rc) {
                                fdel = args[2];
                            } else {
                                rc = PyObject_RichCompareBool(
                                    key,
                                    ptr(impl::template_string<"setter">()),
                                    Py_EQ
                                );
                                if (rc < 0) {
                                    return nullptr;
                                } else if (rc) {
                                    fset = args[2];
                                } else {
                                    PyErr_Format(
                                        PyExc_TypeError,
                                        "unexpected keyword argument '%U'",
                                        key
                                    );
                                    return nullptr;
                                }
                            }
                        } else if (kwcount > 0) {
                            PyObject* key = PyTuple_GET_ITEM(kwnames, 0);
                            int rc = PyObject_RichCompareBool(
                                key,
                                ptr(impl::template_string<"setter">()),
                                Py_EQ
                            );
                            if (rc < 0) {
                                return nullptr;
                            } else if (rc) {
                                fset = args[1];
                            } else {
                                rc = PyObject_RichCompareBool(
                                    key,
                                    ptr(impl::template_string<"deleter">()),
                                    Py_EQ
                                );
                                if (rc < 0) {
                                    return nullptr;
                                } else if (rc) {
                                    fdel = args[1];
                                } else {
                                    PyErr_Format(
                                        PyExc_TypeError,
                                        "unexpected keyword argument '%U'",
                                        key
                                    );
                                    return nullptr;
                                }
                            }
                        }
                    }
                    /// TODO: validate fset and fdel are callable with the expected
                    /// signatures -> This can be done with the Inspect() helper, which
                    /// will extract all overload keys from the function.  I just have
                    /// to confirm that at least one path through the overload trie
                    /// matches the expected signature.

                    if (PyObject_HasAttr(cls, self->name)) {
                        PyErr_Format(
                            PyExc_AttributeError,
                            "attribute '%U' already exists on type '%R'",
                            self->name,
                            cls
                        );
                        return nullptr;
                    }
                    using Property = impl::Property;
                    Property* descr = reinterpret_cast<Property*>(
                        Property::__type__.tp_alloc(&Property::__type__, 0)
                    );
                    if (descr == nullptr) {
                        return nullptr;
                    }
                    try {
                        new (descr) Property(cls, self, fset, fdel);
                    } catch (...) {
                        Py_DECREF(descr);
                        Exception::to_python();
                        return nullptr;
                    }
                    int rc = PyObject_SetAttr(cls, self->name, descr);
                    Py_DECREF(descr);
                    if (rc) {
                        return nullptr;
                    }
                    return Py_NewRef(cls);
                }
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Call the function from Python. */
        static PyObject* __call__(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) noexcept {
            /// TODO: this will have to convert all of the arguments to bertrand types
            /// before proceeding, since Python doesn't retain enough type information
            /// to disambiguate containers based on their contents, and thus the whole
            /// caching mechanism would not work correctly.  The only way to fix this
            /// is to modify call_key to do the translation before hashing the
            /// arguments and searching the trie.
            /// -> What I may be able to do in the future is implement a similar level
            /// of static analysis in Python as I currently have in C++, and then
            /// substitute the call for a private method that does not do any
            /// conversions or search the overload trie, since all of that should be
            /// able to be resolved at compile time via static analysis.  In that case,
            /// this would be a direct function call

            try {



                // forward to private API for underlying call
                return _call(self, args, nargsf, kwnames);

            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Implement the descriptor protocol to generate bound member functions. */
        static PyObject* __get__(
            PyFunction* self,
            PyObject* obj,
            PyObject* type
        ) noexcept {
            try {
                PyObject* cls = reinterpret_cast<PyObject*>(Py_TYPE(self));

                // get the current function's template key and allocate a copy
                Object unbound_key = reinterpret_steal<Object>(PyObject_GetAttr(
                    cls,
                    ptr(impl::template_string<"__template__">())
                ));
                if (unbound_key.is(nullptr)) {
                    return nullptr;
                }
                size_t len = PyTuple_GET_SIZE(ptr(unbound_key));
                Object bound_key = reinterpret_steal<Object>(
                    PyTuple_New(len - 1)
                );
                if (bound_key.is(nullptr)) {
                    return nullptr;
                }

                // the first element encodes the unbound function's return type.  All
                // we need to do is replace the first index of the slice with the new
                // type and exclude the first argument from the unbound key
                PySliceObject* rtype = reinterpret_cast<PySliceObject*>(
                    PyTuple_GET_ITEM(ptr(unbound_key), 0)
                );
                PyObject* slice = PySlice_New(
                    type == Py_None ?
                        reinterpret_cast<PyObject*>(Py_TYPE(obj)) : type,
                    Py_None,
                    rtype->step
                );
                if (slice == nullptr) {
                    return nullptr;
                }
                PyTuple_SET_ITEM(ptr(bound_key), 0, slice);
                for (size_t i = 2; i < len; ++i) {  // skip return type and first arg
                    PyTuple_SET_ITEM(
                        ptr(bound_key),
                        i - 1,
                        Py_NewRef(PyTuple_GET_ITEM(ptr(unbound_key), i))
                    );
                }

                // once the new key is built, we can index the unbound function type to
                // get the corresponding Python class for the bound function
                Object bound_type = reinterpret_steal<Object>(PyObject_GetItem(
                    cls,
                    ptr(bound_key)
                ));
                if (bound_type.is(nullptr)) {
                    return nullptr;
                }

                // finally, we pass both the unbound function and the `self` argument
                // to the bound type's normal Python constructor and return the result
                PyObject* args[] = {ptr(bound_type), self, obj};
                return PyObject_VectorcallMethod(
                    ptr(impl::template_string<"_capture">()),
                    args,
                    3,
                    nullptr
                );

            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* `len(function)` will get the number of overloads that are currently being
        tracked. */
        static Py_ssize_t __len__(PyFunction* self) noexcept {
            return self->overloads.data.size();
        }

        /* Index the function to resolve a specific overload, as if the function were
        being called normally.  Returns `None` if no overload can be found, indicating
        that the function will be called with its base implementation. */
        static PyObject* __getitem__(PyFunction* self, PyObject* specifier) noexcept {
            if (PyTuple_Check(specifier)) {
                Py_INCREF(specifier);
            } else {
                specifier = PyTuple_Pack(1, specifier);
                if (specifier == nullptr) {
                    return nullptr;
                }
            }
            try {
                Object key = reinterpret_steal<Object>(specifier);
                std::optional<PyObject*> func = self->overloads.get(
                    subscript_key(key)
                );
                if (func.has_value()) {
                    return Py_NewRef(func.value() ? func.value() : self);
                }
                Py_RETURN_NONE;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Delete a matching overload, removing it from the overload trie. */
        static int __delitem__(
            PyFunction* self,
            PyObject* specifier,
            PyObject* value
        ) noexcept {
            if (value) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "functions do not support item assignment: use "
                    "`@func.overload` to register an overload instead"
                );
                return -1;
            }
            if (PyTuple_Check(specifier)) {
                Py_INCREF(specifier);
            } else {
                specifier = PyTuple_Pack(1, specifier);
                if (specifier == nullptr) {
                    return -1;
                }
            }
            try {
                Object key = reinterpret_steal<Object>(specifier);
                std::optional<Object> func = self->overloads.remove(
                    subscript_key(key)
                );
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        /* Check whether a given function is contained within the overload trie. */
        static int __contains__(PyFunction* self, PyObject* func) noexcept {
            try {
                for (const auto& data : self->overloads.data) {
                    if (ptr(data.func) == func) {
                        return 1;
                    }
                }
                return 0;
            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        /* Iterate over all overloads stored in the trie. */
        static PyObject* __iter__(PyFunction* self) noexcept {
            try {
                return release(Iterator(
                    self->overloads.data | std::views::transform(
                        [](const Sig::Overloads::Metadata& data) -> Object {
                            return data.func;
                        }
                    )
                ));
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Check whether an object implements this function via the descriptor
        protocol. */
        static PyObject* __instancecheck__(
            PyFunction* self,
            PyObject* instance
        ) noexcept {
            return __subclasscheck__(
                self,
                reinterpret_cast<PyObject*>(Py_TYPE(instance))
            );
        }

        /* Check whether a type implements this function via the descriptor
        protocol. */
        static PyObject* __subclasscheck__(
            PyFunction* self,
            PyObject* cls
        ) noexcept {
            constexpr auto check_intersection = [](
                this auto& check_intersection,
                PyFunction* self,
                PyObject* cls
            ) -> bool {
                if (PyType_IsSubtype(
                    reinterpret_cast<PyTypeObject*>(cls),
                    &impl::FuncIntersect::__type__
                )) {
                    return check_intersection(
                        self,
                        ptr(reinterpret_cast<impl::FuncIntersect*>(cls)->lhs)
                    ) || check_intersection(
                        self,
                        ptr(reinterpret_cast<impl::FuncIntersect*>(cls)->rhs)
                    );
                }
                return reinterpret_cast<PyObject*>(self) == cls;
            };

            try {
                /// NOTE: structural interesection types are considered subclasses of
                /// their component functions, as are exact function matches.
                if (check_intersection(self, cls)) {
                    Py_RETURN_TRUE;
                }

                /// otherwise, we check for an equivalent descriptor on the input class
                if (PyObject_HasAttr(cls, self->name)) {
                    Object attr = reinterpret_steal<Object>(
                        PyObject_GetAttr(cls, self->name)
                    );
                    if (attr.is(nullptr)) {
                        return nullptr;
                    } else if (attr.is(self) || (
                        hasattr<"__wrapped__">(attr) &&
                        getattr<"__wrapped__">(attr).is(self)
                    )) {
                        Py_RETURN_TRUE;
                    }
                }
                Py_RETURN_FALSE;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Supplying a __signature__ attribute allows C++ functions to be introspected
        via the `inspect` module, just like their pure-Python equivalents. */
        static PyObject* __signature__(PyFunction* self, void*) noexcept {
            if (self->pysignature) {
                return Py_NewRef(self->pysignature);
            }

            try {
                Object inspect = reinterpret_steal<Object>(PyImport_Import(
                    ptr(impl::template_string<"inspect">())
                ));
                if (inspect.is(nullptr)) {
                    return nullptr;
                }

                // if this function captures a Python function, forward to it
                if (self->pyfunc) {
                    Object signature = reinterpret_steal<Object>(PyObject_GetAttr(
                        ptr(inspect),
                        ptr(impl::template_string<"signature">())
                    ));
                    if (signature.is(nullptr)) {
                        return nullptr;
                    }
                    return PyObject_CallOneArg(
                        ptr(signature),
                        self->pyfunc
                    );
                }

                // otherwise, we need to build a signature object ourselves
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
                PyObject* args[] = {nullptr, ptr(tuple), ptr(return_type)};
                Object kwnames = reinterpret_steal<Object>(
                    PyTuple_Pack(1, ptr(impl::template_string<"return_annotation">()))
                );
                return PyObject_Vectorcall(
                    ptr(Signature),
                    args + 1,
                    1 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    ptr(kwnames)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Default `repr()` demangles the function name + signature. */
        static PyObject* __repr__(PyFunction* self) noexcept {
            try {
                constexpr std::string demangled =
                    impl::demangle(typeid(Function<F>).name());
                std::string str = "<" + demangled + " at " +
                    std::to_string(reinterpret_cast<size_t>(self)) + ">";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        /* Implements the Python constructor without any */

        /* Implements the Python call operator without any type safety checks. */
        static PyObject* _call(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) noexcept {
            try {
                // check for overloads and forward if one is found
                if (!self->overloads.data.empty()) {
                    PyObject* overload = self->overloads.search(
                        call_key(args, nargsf, kwnames)
                    );
                    if (overload) {
                        return PyObject_Vectorcall(
                            overload,
                            args,
                            nargsf,
                            kwnames
                        );
                    }
                }

                // if this function wraps a captured Python function, then we can
                // immediately forward to it as an optimization
                if (!self->pyfunc.is(nullptr)) {
                    return PyObject_Vectorcall(
                        ptr(self->pyfunc),
                        args,
                        nargsf,
                        kwnames
                    );
                }

                /// TODO: fix the last bugs in the call operator.

                // otherwise, we fall back to the base C++ implementation, which
                // requires us to translate the Python arguments according to the
                // template signature
                return []<size_t... Is>(
                    std::index_sequence<Is...>,
                    PyFunction* self,
                    PyObject* const* args,
                    Py_ssize_t nargsf,
                    PyObject* kwnames
                ) {
                    size_t nargs = PyVectorcall_NARGS(nargsf);
                    if constexpr (!Sig::has_args) {
                        constexpr size_t expected = Sig::n - Sig::n_kwonly;
                        if (nargs > expected) {
                            throw TypeError(
                                "expected at most " + std::to_string(expected) +
                                " positional arguments, but received " +
                                std::to_string(nargs)
                            );
                        }
                    }
                    size_t kwcount = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
                    if constexpr (!Sig::has_kwargs) {
                        for (size_t i = 0; i < kwcount; ++i) {
                            Py_ssize_t len;
                            const char* name = PyUnicode_AsUTF8AndSize(
                                PyTuple_GET_ITEM(kwnames, i),
                                &len
                            );
                            if (name == nullptr) {
                                Exception::from_python();
                            } else if (!Sig::callback(std::string_view(name, len))) {
                                throw TypeError(
                                    "unexpected keyword argument '" +
                                    std::string(name, len) + "'"
                                );
                            }
                        }
                    }
                    if constexpr (std::is_void_v<typename Sig::Return>) {
                        self->func(
                            {impl::Arguments<Target...>::template from_python<Is>(
                                self->defaults,
                                args,
                                nargs,
                                kwnames,
                                kwcount
                            )}...
                        );
                        Py_RETURN_NONE;
                    } else {
                        return release(as_object(
                            self->func(
                                {impl::Arguments<Target...>::template from_python<Is>(
                                    self->defaults,
                                    args,
                                    nargs,
                                    kwnames,
                                    kwcount
                                )}...
                            )
                        ));
                    }
                }(
                    std::make_index_sequence<Sig::n>{},
                    self,
                    args,
                    nargsf,
                    kwnames
                );

            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* _subtrie_len(PyFunction* self, PyObject* type) noexcept {
            try {
                size_t len = 0;
                for (const typename Sig::Overloads::Metadata& data :
                    self->overloads.match(type)
                ) {
                    ++len;
                }
                return PyLong_FromSize_t(len);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* _subtrie_iter(PyFunction* self, PyObject* type) noexcept {
            try {
                return release(Iterator(
                    self->overloads.match(type) | std::views::transform(
                        [](const typename Sig::Overloads::Metadata& data) -> Object {
                            return data.func;
                        }
                    )
                ));
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static PyObject* _subtrie_contains(
            PyFunction* self,
            PyObject* const* args,
            Py_ssize_t nargsf
        ) noexcept {
            try {
                for (const typename Sig::Overloads::Metadata& data :
                    self->overloads.match(args[0])
                ) {
                    if (data.func == args[1]) {
                        Py_RETURN_TRUE;
                    }
                }
                Py_RETURN_FALSE;
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /// TODO: all this constructor crap also has to be reflected for bound methods.

        static PyObject* validate_signature(PyObject* func, const impl::Inspect& signature) {
            // ensure at least one possible return type exactly matches the
            // expected template signature
            Object rtype = std::is_void_v<typename Sig::Return> ?
                reinterpret_borrow<Object>(Py_None) :
                Object(Type<typename Sig::Return>());
            bool match = false;
            for (PyObject* returns : signature.returns()) {
                if (rtype.is(returns)) {
                    match = true;
                    break;
                }
            }
            if (!match) {
                throw TypeError(
                    "base function must return " + repr(rtype) + ", not: '" +
                    repr(reinterpret_borrow<Object>(signature.returns()[0])) +
                    "'"
                );
            }

            // ensure at least one complete parameter list exactly matches the
            // expected template signature
            constexpr auto validate = []<size_t... Is>(
                std::index_sequence<Is...>,
                impl::Inspect& signature,
                const auto& key
            ) {
                return (validate_parameter<Is>(key[Is]) && ...);
            };
            match = false;
            for (const auto& key : signature) {
                if (
                    key.size() == Sig::n &&
                    validate(std::make_index_sequence<Sig::n>{}, signature, key)
                ) {
                    match = true;
                    break;
                }
            }
            if (!match) {
                throw TypeError(
                    /// TODO: improve this error message by printing out the
                    /// expected signature.  Maybe I can just get the repr of the
                    /// current function type?
                    "no match for parameter list"
                );
            }

            // extract default values from the signature
            return []<size_t... Js>(std::index_sequence<Js...>, impl::Inspect& sig) {
                return typename Sig::Defaults{extract_default<Js>(sig)...};
            }(std::make_index_sequence<Sig::n_opt>{}, signature);
        }

        template <size_t I>
        static bool validate_parameter(const impl::Param& param) {
            using T = Sig::template at<I>;
            return (
                param.name == impl::ArgTraits<T>::name &&
                param.kind == impl::ArgTraits<T>::kind &&
                param.type == ptr(Type<typename impl::ArgTraits<T>::type>())
            );
        }

        template <size_t J>
        static Object extract_default(impl::Inspect& signature) {
            Object default_value = getattr<"default">(
                signature.at(Sig::Defaults::template rfind<J>)
            );
            if (default_value.is(getattr<"empty">(signature.signature))) {
                throw TypeError(
                    "missing default value for parameter '" +
                    impl::ArgTraits<typename Sig::Defaults::template at<J>>::name + "'"
                );
            }
            return default_value;
        }

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

            PyObject* args[] = {
                nullptr,
                ptr(name),
                ptr(kind),
                ptr(default_value),
                ptr(annotation),
            };
            Object kwnames = reinterpret_steal<Object>(
                PyTuple_Pack(4,
                    ptr(impl::template_string<"name">()),
                    ptr(impl::template_string<"kind">()),
                    ptr(impl::template_string<"default">()),
                    ptr(impl::template_string<"annotation">())
                )
            );
            Object result = reinterpret_steal<Object>(PyObject_Vectorcall(
                ptr(Parameter),
                args + 1,
                0 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                ptr(kwnames)
            ));
            if (result.is(nullptr)) {
                Exception::from_python();
            }
            return result;
        }

        static impl::Params<std::vector<impl::Param>> call_key(
            PyObject* const* args,
            Py_ssize_t nargsf,
            PyObject* kwnames
        ) {
            size_t hash = 0;
            Py_ssize_t nargs = PyVectorcall_NARGS(nargsf);
            Py_ssize_t kwcount = kwnames ? PyTuple_GET_SIZE(kwnames) : 0;
            std::vector<impl::Param> key;
            key.reserve(nargs + kwcount);

            for (Py_ssize_t i = 0; i < nargs; ++i) {
                PyObject* arg = args[i];
                key.push_back({
                    "",
                    reinterpret_cast<PyObject*>(Py_TYPE(arg)),
                    impl::ArgKind::POS
                });
                hash = impl::hash_combine(
                    hash,
                    key.back().hash(Sig::seed, Sig::prime)
                );
            }

            for (Py_ssize_t i = 0; i < kwcount; ++i) {
                PyObject* name = PyTuple_GET_ITEM(kwnames, i);
                key.push_back({
                    impl::Param::get_name(name),
                    reinterpret_cast<PyObject*>(Py_TYPE(args[nargs + i])),
                    impl::ArgKind::KW
                });
                hash = impl::hash_combine(
                    hash,
                    key.back().hash(Sig::seed, Sig::prime)
                );
            }
            return {std::move(key), hash};
        }

        static impl::Params<std::vector<impl::Param>> subscript_key(
            const Object& specifier
        ) {
            size_t hash = 0;
            Py_ssize_t size = PyTuple_GET_SIZE(ptr(specifier));
            std::vector<impl::Param> key;
            key.reserve(size);

            std::unordered_set<std::string_view> names;
            Py_ssize_t kw_idx = std::numeric_limits<Py_ssize_t>::max();
            for (Py_ssize_t i = 0; i < size; ++i) {
                PyObject* item = PyTuple_GET_ITEM(ptr(specifier), i);

                // slices represent keyword arguments
                if (PySlice_Check(item)) {
                    PySliceObject* slice = reinterpret_cast<PySliceObject*>(item);
                    if (!PyUnicode_Check(slice->start)) {
                        throw TypeError(
                            "expected a keyword argument name as first "
                            "element of slice, not " + repr(
                                reinterpret_borrow<Object>(slice->start)
                            )
                        );
                    }
                    std::string_view name = impl::Param::get_name(slice->start);
                    if (names.contains(name)) {
                        throw TypeError(
                            "duplicate keyword argument: " + std::string(name)
                        );
                    } else if (slice->step != Py_None) {
                        throw TypeError(
                            "keyword argument cannot have a third slice element: " +
                            repr(reinterpret_borrow<Object>(slice->step))
                        );
                    }
                    key.push_back({
                        name,
                        PyType_Check(slice->stop) ?
                            slice->stop :
                            reinterpret_cast<PyObject*>(Py_TYPE(slice->stop)),
                        impl::ArgKind::KW
                    });
                    hash = impl::hash_combine(
                        hash,
                        key.back().hash(Sig::seed, Sig::prime)
                    );
                    kw_idx = i;
                    names.insert(name);

                // all other objects are positional arguments
                } else {
                    if (i > kw_idx) {
                        throw TypeError(
                            "positional argument follows keyword argument"
                        );
                    }
                    key.push_back({
                        "",
                        PyType_Check(item) ?
                            item :
                            reinterpret_cast<PyObject*>(Py_TYPE(item)),
                        impl::ArgKind::POS
                    });
                    hash = impl::hash_combine(
                        hash,
                        key.back().hash(Sig::seed, Sig::prime)
                    );
                }
            }

            return {std::move(key), hash};
        }

        inline static PyNumberMethods number = {
            .nb_and = reinterpret_cast<binaryfunc>(&impl::FuncIntersect::__and__),
            .nb_or = reinterpret_cast<binaryfunc>(&impl::FuncUnion::__or__),
        };

        inline static PyMethodDef methods[] = {
            {
                "overload",
                reinterpret_cast<PyCFunction>(&overload),
                METH_O,
                PyDoc_STR(
R"doc()doc"
                )
            },
            {
                "clear",
                reinterpret_cast<PyCFunction>(&clear),
                METH_NOARGS,
                PyDoc_STR(
R"doc()doc"
                )
            },
            {
                "flush",
                reinterpret_cast<PyCFunction>(&flush),
                METH_NOARGS,
                PyDoc_STR(
R"doc()doc"
                )
            },
            {
                "method",
                reinterpret_cast<PyCFunction>(&method),
                METH_O,
                PyDoc_STR(
R"doc()doc"
                )
            },
            {
                "classmethod",
                reinterpret_cast<PyCFunction>(&classmethod),
                METH_O,
                PyDoc_STR(
R"doc()doc"
                )
            },
            {
                "staticmethod",
                reinterpret_cast<PyCFunction>(&staticmethod),
                METH_O,
                PyDoc_STR(
R"doc()doc"
                )
            },
            {
                "property",
                reinterpret_cast<PyCFunction>(&property),
                METH_FASTCALL | METH_KEYWORDS,
                PyDoc_STR(
R"doc()doc"
                )
            },
            {
                "_call",
                reinterpret_cast<PyCFunction>(&_call),
                METH_FASTCALL | METH_KEYWORDS,
                nullptr
            },
            {
                "_subtrie_len",
                reinterpret_cast<PyCFunction>(&_subtrie_len),
                METH_O,
                nullptr
            },
            {
                "_subtrie_iter",
                reinterpret_cast<PyCFunction>(&_subtrie_iter),
                METH_O,
                nullptr
            },
            {
                "_subtrie_contains",
                reinterpret_cast<PyCFunction>(&_subtrie_contains),
                METH_FASTCALL,
                nullptr
            },
            {nullptr}
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
            },
            {nullptr}
        };

    };

    /* Bound member function type.  Must be constructed with a corresponding `self`
    parameter, which will be inserted as the first argument to a call according to
    Python style. */
    template <typename Sig> requires (Sig::has_self)
    struct PyFunction<Sig> : def<PyFunction<Sig>, Function>, PyObject {
        static constexpr StaticStr __doc__ =
R"doc(A bound member function descriptor.

Notes
-----
This type is equivalent to Python's internal `types.MethodType`, which
describes the return value of a method descriptor when accessed from an
instance of an enclosing class.  The only difference is that this type is
implemented in C++, and thus has a unique instantiation for each signature.

Additionally, it must be noted that instances of this type must be constructed
with an appropriate `self` parameter, which is inserted as the first argument
to the underlying C++/Python function when called, according to Python style.
As such, it is not possible for an instance of this type to represent an
unbound function object; those are always represented as a non-member function
type instead.  By templating `py::Function<...>` on a member function pointer,
you are directly indicating the presence of the bound `self` parameter, in a
way that encodes this information into the type systems of both languages
simultaneously.

In essence, all this type does is hold a reference to both an equivalent
non-member function, as well as a reference to the `self` object that the
function is bound to.  All operations will be simply forwarded to the
underlying non-member function, including overloads, introspection, and so on,
but with the `self` argument already accounted for.

Examples
--------
>>> from bertrand import Function
>>> Function[::int, "x": int, "y": int]
<class 'py::Function<py::Int(*)(py::Arg<"x", py::Int>, py::Arg<"y", py::Int>)>'>
>>> Function[::None, "*objects": object, "sep": str: ..., "end": str: ..., "file": object: ..., "flush": bool: ...]
<class 'py::Function<void(*)(py::Arg<"objects", py::Object>::args, py::Arg<"sep", py::Str>::opt, py::Arg<"end", py::Str>::opt, py::Arg<"file", py::Object>::opt, py::Arg<"flush", py::Bool>::opt)>'>
>>> Function[list[object]::None, "*", "key": object: ..., "reverse": bool: ...]
<class 'py::Function<void(py::List<py::Object>::*)(py::Arg<"key", py::Object>::kw::opt, py::Arg<"reverse", py::Bool>::kw::opt)>'>
>>> Function[type[bytes]::bytes, "string": str, "/"]
<class 'py::Function<py::Bytes(Type<py::Bytes>::*)(py::Arg<"string", py::Str>::pos)>'>

Each of these accessors will resolve to a unique Python type that wraps a
specific C++ function signature.

The 2nd example shows the template signature of the built-in `print()`
function, which returns void and accepts variadic positional arguments of any
type, followed by keyword arguments of various types, all of which are optional
(indicated by the trailing `...` syntax).

The 3rd example represents a bound member function corresponding to the
built-in `list.sort()` method, which accepts two optional keyword-only
arguments, where the list can contain any type.  The `*` delimiter works
just like a standard Python function declaration in this case, with equivalent
semantics.  The type of the bound `self` parameter is given on the left side of
the `list[object]::None` return type, which can be thought of similar to a C++
`::` scope accessor.  The type on the right side is the method's normal return
type, which in this case is `None`.

The 4th example represents a class method corresponding to the built-in
`bytes.fromhex()` method, which accepts a single, required, positional-only
argument of type `str`.  The `/` delimiter is used to indicate positional-only
arguments similar to `*`.  The type of the `self` parameter in this case is
given as a subscription of `type[]`, which indicates that the bound `self`
parameter is a type object, and thus the method is a class method.)doc";

        vectorcallfunc call = reinterpret_cast<vectorcallfunc>(__call__);
        PyObject* __wrapped__;
        PyObject* __self__;

        explicit PyFunction(PyObject* __wrapped__, PyObject* __self__) noexcept :
            __wrapped__(Py_NewRef(__wrapped__)), __self__(Py_NewRef(__self__))
        {}

        ~PyFunction() noexcept {
            Py_XDECREF(__wrapped__);
            Py_XDECREF(__self__);
        }

        static void __dealloc__(PyFunction* self) noexcept {
            PyObject_GC_UnTrack(self);
            self->~PyFunction();
            Py_TYPE(self)->tp_free(self);
        }

        static PyObject* __new__(
            PyTypeObject* cls,
            PyObject* args,
            PyObject* kwds
        ) noexcept {
            try {
                PyFunction* self = reinterpret_cast<PyFunction*>(cls->tp_alloc(cls, 0));
                if (self == nullptr) {
                    return nullptr;
                }
                self->call = reinterpret_cast<vectorcallfunc>(__call__);
                self->__wrapped__ = nullptr;
                self->__self__ = nullptr;
                return reinterpret_cast<PyObject*>(self);
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static int __init__(
            PyFunction* self,
            PyObject* args,
            PyObject* kwds
        ) noexcept {
            try {
                size_t nargs = PyTuple_GET_SIZE(args);
                if (nargs != 2 || kwds != nullptr) {
                    PyErr_Format(
                        PyExc_TypeError,
                        "expected exactly 2 positional-only arguments, but "
                        "received %zd",
                        nargs
                    );
                    return -1;
                }
                PyObject* func = PyTuple_GET_ITEM(args, 0);
                impl::Inspect signature = {func, Sig::seed, Sig::prime};

                /// TODO: do everything from the unbound constructor, but also ensure
                /// that the self argument matches the expected type.
                /// -> NOTE: this must assert that the function being passed in has a
                /// `__self__` attribute that matches the expected type, which is true
                /// for both Python bound methods and my own bound methods.

            } catch (...) {
                Exception::to_python();
                return -1;
            }
        }

        /// TODO: I'll need a Python-level __init__/__new__ method that
        /// constructs a new instance of this type, which will be called
        /// when the descriptor is accessed.

        template <StaticStr ModName>
        static Type<Function> __export__(Module<ModName> bindings);
        static Type<Function> __import__();

        static PyObject* __call__(
            PyFunction* self,
            PyObject* const* args,
            size_t nargsf,
            PyObject* kwnames
        ) noexcept {
            try {
                /// NOTE: Python includes an optimization of the vectorcall protocol
                /// for bound functions that can temporarily forward the correct `self`
                /// argument without reallocating the underlying array, which we can
                /// take advantage of if possible.
                size_t nargs = PyVectorcall_NARGS(nargsf);
                if (nargsf & PY_VECTORCALL_ARGUMENTS_OFFSET) {
                    PyObject** arr = const_cast<PyObject**>(args) - 1;
                    PyObject* temp = arr[0];
                    arr[0] = self->__self__;
                    PyObject* result = PyObject_Vectorcall(
                        self->__wrapped__,
                        arr,
                        nargs + 1,
                        kwnames
                    );
                    arr[0] = temp;
                    return result;
                }

                /// otherwise, we have to heap allocate a new array and copy the arguments
                size_t n = nargs + (kwnames ? PyTuple_GET_SIZE(kwnames) : 0);
                PyObject** arr = new PyObject*[n + 1];
                arr[0] = self->__self__;
                for (size_t i = 0; i < n; ++i) {
                    arr[i + 1] = args[i];
                }
                PyObject* result = PyObject_Vectorcall(
                    self->__wrapped__,
                    arr,
                    nargs + 1,
                    kwnames
                );
                delete[] arr;
                return result;

            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        static Py_ssize_t __len__(PyFunction* self) noexcept {
            PyObject* result = PyObject_CallMethodOneArg(
                self->__wrapped__,
                ptr(impl::template_string<"_subtrie_len">()),
                PyType_Check(self->__self__) ?
                    self->__self__ :
                    reinterpret_cast<PyObject*>(Py_TYPE(self->__self__))
            );
            if (result == nullptr) {
                return -1;
            }
            Py_ssize_t len = PyLong_AsSsize_t(result);
            Py_DECREF(result);
            return len;
        }

        /* Subscripting a bound method will forward to the unbound method, prepending
        the key with the `self` argument. */
        static PyObject* __getitem__(
            PyFunction* self,
            PyObject* specifier
        ) noexcept {
            if (PyTuple_Check(specifier)) {
                Py_ssize_t len = PyTuple_GET_SIZE(specifier);
                PyObject* tuple = PyTuple_New(len + 1);
                if (tuple == nullptr) {
                    return nullptr;
                }
                PyTuple_SET_ITEM(tuple, 0, Py_NewRef(self->__self__));
                for (Py_ssize_t i = 0; i < len; ++i) {
                    PyTuple_SET_ITEM(
                        tuple,
                        i + 1,
                        Py_NewRef(PyTuple_GET_ITEM(specifier, i))
                    );
                }
                specifier = tuple;
            } else {
                specifier = PyTuple_Pack(2, self->__self__, specifier);
                if (specifier == nullptr) {
                    return nullptr;
                }
            }
            PyObject* result = PyObject_GetItem(self->__wrapped__, specifier);
            Py_DECREF(specifier);
            return result;
        }

        /* Deleting an overload from a bound method will forward the deletion to the
        unbound method, prepending the key with the `self` argument. */
        static int __delitem__(
            PyFunction* self,
            PyObject* specifier,
            PyObject* value
        ) noexcept {
            if (value) {
                PyErr_SetString(
                    PyExc_TypeError,
                    "functions do not support item assignment: use "
                    "`@func.overload` to register an overload instead"
                );
                return -1;
            }
            if (PyTuple_Check(specifier)) {
                Py_ssize_t len = PyTuple_GET_SIZE(specifier);
                PyObject* tuple = PyTuple_New(len + 1);
                if (tuple == nullptr) {
                    return -1;
                }
                PyTuple_SET_ITEM(tuple, 0, Py_NewRef(self->__self__));
                for (Py_ssize_t i = 0; i < len; ++i) {
                    PyTuple_SET_ITEM(
                        tuple,
                        i + 1,
                        Py_NewRef(PyTuple_GET_ITEM(specifier, i))
                    );
                }
                specifier = tuple;
            } else {
                specifier = PyTuple_Pack(2, self->__self__, specifier);
                if (specifier == nullptr) {
                    return -1;
                }
            }
            int result = PyObject_DelItem(self->__wrapped__, specifier);
            Py_DECREF(specifier);
            return result;
        }

        static int __contains__(PyFunction* self, PyObject* func) noexcept {
            PyObject* args[] = {
                self->__wrapped__,
                PyType_Check(self->__self__) ?
                    self->__self__ :
                    reinterpret_cast<PyObject*>(Py_TYPE(self->__self__)),
                func
            };
            PyObject* result = PyObject_VectorcallMethod(
                ptr(impl::template_string<"_subtrie_contains">()),
                args,
                3 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                nullptr
            );
            if (result == nullptr) {
                return -1;
            }
            int contains = PyObject_IsTrue(result);
            Py_DECREF(result);
            return contains;
        }

        static PyObject* __iter__(PyFunction* self) noexcept {
            return PyObject_CallMethodOneArg(
                self->__wrapped__,
                ptr(impl::template_string<"_subtrie_iter">()),
                PyType_Check(self->__self__) ?
                    self->__self__ :
                    reinterpret_cast<PyObject*>(Py_TYPE(self->__self__))
            );
        }

        static PyObject* __signature__(PyFunction* self, void*) noexcept {
            try {
                Object inspect = reinterpret_steal<Object>(PyImport_Import(
                    ptr(impl::template_string<"inspect">())
                ));
                if (inspect.is(nullptr)) {
                    return nullptr;
                }
                Object signature = PyObject_CallOneArg(
                    ptr(getattr<"signature">(inspect)),
                    self->__wrapped__
                );
                if (signature.is(nullptr)) {
                    return nullptr;
                }
                Object values = getattr<"values">(
                    getattr<"parameters">(signature)
                );
                size_t size = len(values);
                Object parameters = reinterpret_steal<Object>(
                    PyTuple_New(size - 1)
                );
                if (parameters.is(nullptr)) {
                    return nullptr;
                }
                auto it = begin(values);
                auto stop = end(values);
                ++it;
                for (size_t i = 0; it != stop; ++it, ++i) {
                    PyTuple_SET_ITEM(
                        ptr(parameters),
                        i,
                        Py_NewRef(ptr(*it))
                    );
                }
                PyObject* args[] = {nullptr, ptr(parameters)};
                Object kwnames = reinterpret_steal<Object>(
                    PyTuple_Pack(1, ptr(impl::template_string<"parameters">()))
                );
                return PyObject_Vectorcall(
                    ptr(getattr<"replace">(signature)),
                    args + 1,
                    0 | PY_VECTORCALL_ARGUMENTS_OFFSET,
                    ptr(kwnames)
                );
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

        /* Default `repr()` reflects Python conventions for bound methods. */
        static PyObject* __repr__(PyFunction* self) noexcept {
            try {
                std::string str =
                    "<bound method " +
                    impl::demangle(Py_TYPE(self->__self__)->tp_name) + ".";
                Py_ssize_t len;
                const char* name = PyUnicode_AsUTF8AndSize(
                    self->__wrapped__->name,
                    &len
                );
                if (name == nullptr) {
                    return nullptr;
                }
                str += std::string(name, len) + " of ";
                str += repr(reinterpret_borrow<Object>(self->__self__)) + ">";
                return PyUnicode_FromStringAndSize(str.c_str(), str.size());
            } catch (...) {
                Exception::to_python();
                return nullptr;
            }
        }

    private:

        /* A private, class-level constructor called internally by the descriptor
        protocol to avoid any superfluous argument validation when binding methods. */
        static PyObject* _capture(
            PyTypeObject* cls,
            PyObject* const* args,
            Py_ssize_t nargsf
        ) noexcept {
            PyObject* result = cls->tp_alloc(cls, 0);
            if (result == nullptr) {
                return nullptr;
            }
            try {
                new (result) PyFunction(args[0], args[1]);
            } catch (...) {
                Py_DECREF(result);
                Exception::to_python();
                return nullptr;
            }
            PyObject_GC_Track(result);
            return result;
        }

        inline static PyNumberMethods number = {
            .nb_and = reinterpret_cast<binaryfunc>(&impl::FuncIntersect::__and__),
            .nb_or = reinterpret_cast<binaryfunc>(&impl::FuncUnion::__or__),
        };

        inline static PyMethodDef methods[] = {
            {
                "_capture",
                reinterpret_cast<PyCFunction>(&_capture),
                METH_CLASS | METH_FASTCALL,
                nullptr
            },
            {nullptr}
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
            },
            {nullptr}
        };

    };

public:
    using __python__ = PyFunction<impl::Signature<F>>;

    Function(PyObject* p, borrowed_t t) : Object(p, t) {}
    Function(PyObject* p, stolen_t t) : Object(p, t) {}

    template <typename T = Function> requires (__initializer__<T>::enable)
    Function(const std::initializer_list<typename __initializer__<T>::type>& init) :
        Object(__initializer__<T>{}(init))
    {}

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


/// TODO: I would also need some way to disambiguate static functions from member
/// functions when doing CTAD.  This is probably accomplished by providing an extra
/// argument to the constructor which holds the `self` value, and is implicitly
/// convertible to the function's first parameter type.  In that case, the CTAD
/// guide would always deduce to a member function over a static function.  If the
/// extra argument is given and is not convertible to the first parameter type, then
/// we issue a compile error, and if the extra argument is not given at all, then we
/// interpret it as a static function.


/// TODO: so providing the extra `self` argument would be a way to convert a static
/// function pointer into a member function pointer.  The only problem is what happens
/// if the first argument type is `std::string`?  Perhaps you need to pass the self
/// parameter as an initializer list.  Alternatively, the initializer list could be
/// necessary for the function name/docstring, which would prevent conflicts with
/// the `self` wrapper.

/// -> What if you provide self as an initializer list, together with the function
/// itself?


/*
    auto func = py::Function(
        "subtract",
        "a simple example function",
        {
            foo,
            [](py::Arg<"x", const Foo&> x, py::Arg<"y", int>::opt y) {
                return x.value - y.value;
            }
        },
        py::arg<"y"> = 2
    );
*/


/* CTAD guides for construction from function pointer types. */
template <typename Func, typename... Defaults>
    requires (
        impl::Signature<Func>::enable &&
        impl::Signature<Func>::Defaults::template Bind<Defaults...>::enable
    )
Function(Func, Defaults&&...)
    -> Function<typename impl::Signature<Func>::type>;
template <typename Func, typename... Defaults>
    requires (
        impl::Signature<Func>::enable &&
        impl::Signature<Func>::Defaults::template Bind<Defaults...>::enable
    )
Function(std::string, Func, Defaults&&...)
    -> Function<typename impl::Signature<Func>::type>;
template <typename Func, typename... Defaults>
    requires (
        impl::Signature<Func>::enable &&
        impl::Signature<Func>::Defaults::template Bind<Defaults...>::enable
    )
Function(std::string, std::string, Func, Defaults&&...)
    -> Function<typename impl::Signature<Func>::type>;


/// TODO: if you pass in a static function + a self parameter, then CTAD will deduce to
/// a member function of the appropriate type.


/// TODO: modify these to account for conversion from static functions to member functions
template <typename Self, typename Func, typename... Defaults>
    requires (
        impl::Signature<Func>::enable &&
        (impl::Signature<Func>::has_self ?
            std::same_as<Self, typename impl::Signature<Func>::Self> :
            impl::Signature<Func>::n > 0 && std::convertible_to<
                Self,
                typename impl::ArgTraits<typename impl::Signature<Func>::template at<0>>::type
            >
        ) &&
        impl::Signature<Func>::Defaults::template Bind<Defaults...>::enable
    )
Function(std::pair<Self&&, Func>, Defaults&&...)
    -> Function<typename impl::Signature<Func>::type>;
template <typename Self, typename Func, typename... Defaults>
    requires (
        impl::Signature<Func>::enable &&
        std::same_as<Self, typename impl::Signature<Func>::Self> &&
        impl::Signature<Func>::Defaults::template Bind<Defaults...>::enable
    )
Function(std::string, std::pair<Self&&, Func>, Defaults&&...)
    -> Function<typename impl::Signature<Func>::type>;
template <typename Self, typename Func, typename... Defaults>
    requires (
        impl::Signature<Func>::enable &&
        std::same_as<Self, typename impl::Signature<Func>::Self> &&
        impl::Signature<Func>::Defaults::template Bind<Defaults...>::enable
    )
Function(std::string, std::string, std::pair<Self&&, Func>, Defaults&&...)
    -> Function<typename impl::Signature<Func>::type>;


/* CTAD guides for construction from function objects implementing a call operator. */
template <typename Func, typename... Defaults>
    requires (
        !impl::Signature<Func>::enable &&
        !impl::inherits<Func, impl::FunctionTag> &&
        impl::has_call_operator<Func> &&
        impl::Signature<decltype(&Func::operator())>::enable &&
        impl::Signature<decltype(&Func::operator())>::Defaults::
            template Bind<Defaults...>::enable
    )
Function(Func, Defaults&&...)
    -> Function<typename impl::Signature<decltype(&Func::operator())>::type>;
template <typename Func, typename... Defaults>
    requires (
        !impl::Signature<Func>::enable &&
        !impl::inherits<Func, impl::FunctionTag> &&
        impl::has_call_operator<Func> &&
        impl::Signature<decltype(&Func::operator())>::enable &&
        impl::Signature<decltype(&Func::operator())>::Defaults::
            template Bind<Defaults...>::enable
    )
Function(std::string, Func, Defaults&&...)
    -> Function<typename impl::Signature<decltype(&Func::operator())>::type>;
template <typename Func, typename... Defaults>
    requires (
        !impl::Signature<Func>::enable &&
        !impl::inherits<Func, impl::FunctionTag> &&
        impl::has_call_operator<Func> &&
        impl::Signature<decltype(&Func::operator())>::enable &&
        impl::Signature<decltype(&Func::operator())>::Defaults::
            template Bind<Defaults...>::enable
    )
Function(std::string, std::string, Func, Defaults&&...)
    -> Function<typename impl::Signature<decltype(&Func::operator())>::type>;


template <impl::inherits<impl::FunctionTag> F>
struct __template__<F> {
    using Func = std::remove_reference_t<F>;

    template <size_t I, size_t PosOnly, size_t KwOnly>
    static void get(PyObject* tuple, size_t& offset) {
        using T = Func::template at<I>;
        Type<typename impl::ArgTraits<T>::type> type;

        /// NOTE: `/` and `*` argument delimiters must be inserted where necessary to
        /// model positional-only and keyword-only arguments correctly in Python.
        if constexpr (
            (I == PosOnly) ||
            ((I == Func::n - 1) && impl::ArgTraits<T>::posonly())
        ) {
            PyObject* str = PyUnicode_FromStringAndSize("/", 1);
            if (str == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(tuple, I + offset, str);
            ++offset;

        } else if constexpr (I == KwOnly) {
            PyObject* str = PyUnicode_FromStringAndSize("*", 1);
            if (str == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(tuple, I + offset, str);
            ++offset;
        }

        if constexpr (impl::ArgTraits<T>::posonly()) {
            if constexpr (impl::ArgTraits<T>::name.empty()) {
                if constexpr (impl::ArgTraits<T>::opt()) {
                    PyObject* slice = PySlice_New(
                        Type<typename impl::ArgTraits<T>::type>(),
                        Py_Ellipsis,
                        Py_None
                    );
                    if (slice == nullptr) {
                        Exception::from_python();
                    }
                    PyTuple_SET_ITEM(tuple, I + offset, slice);
                } else {
                    PyTuple_SET_ITEM(tuple, I + offset, ptr(type));
                }
            } else {
                Object name = reinterpret_steal<Object>(
                    PyUnicode_FromStringAndSize(
                        impl::ArgTraits<T>::name,
                        impl::ArgTraits<T>::name.size()
                    )
                );
                if (name.is(nullptr)) {
                    Exception::from_python();
                }
                if constexpr (impl::ArgTraits<T>::opt()) {
                    PyObject* slice = PySlice_New(
                        ptr(name),
                        ptr(type),
                        Py_Ellipsis
                    );
                    if (slice == nullptr) {
                        Exception::from_python();
                    }
                    PyTuple_SET_ITEM(tuple, I + offset, slice);
                } else {
                    PyObject* slice = PySlice_New(
                        ptr(name),
                        ptr(type),
                        Py_None
                    );
                    if (slice == nullptr) {
                        Exception::from_python();
                    }
                    PyTuple_SET_ITEM(tuple, I + offset, slice);
                }
            }

        } else if constexpr (impl::ArgTraits<T>::kw()) {
            Object name = reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(
                    impl::ArgTraits<T>::name,
                    impl::ArgTraits<T>::name.size()
                )
            );
            if (name.is(nullptr)) {
                Exception::from_python();
            }
            PyObject* slice = PySlice_New(
                ptr(name),
                ptr(type),
                impl::ArgTraits<T>::opt() ? Py_Ellipsis : Py_None
            );
            if (slice == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(tuple, I + offset, slice);

        } else if constexpr (impl::ArgTraits<T>::args()) {
            Object name = reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(
                    "*" + impl::ArgTraits<T>::name,
                    impl::ArgTraits<T>::name.size() + 1
                )
            );
            if (name.is(nullptr)) {
                Exception::from_python();
            }
            PyObject* slice = PySlice_New(
                ptr(name),
                ptr(type),
                Py_None
            );
            if (slice == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(tuple, I + offset, slice);

        } else if constexpr (impl::ArgTraits<T>::kwargs()) {
            Object name = reinterpret_steal<Object>(
                PyUnicode_FromStringAndSize(
                    "**" + impl::ArgTraits<T>::name,
                    impl::ArgTraits<T>::name.size() + 2
                )
            );
            if (name.is(nullptr)) {
                Exception::from_python();
            }
            PyObject* slice = PySlice_New(
                ptr(name),
                ptr(type),
                Py_None
            );
            if (slice == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(tuple, I + offset, slice);

        } else {
            static_assert(false, "unrecognized argument kind");
        }
    }

    static Object operator()() {
        Object result = reinterpret_steal<Object>(
            PyTuple_New(Func::n + 1 + Func::has_posonly + Func::has_kwonly)
        );
        if (result.is(nullptr)) {
            Exception::from_python();
        }

        Object rtype = std::is_void_v<typename Func::Return> ?
            reinterpret_borrow<Object>(Py_None) :
            Object(Type<typename impl::ArgTraits<typename Func::Return>::type>());
        if constexpr (Func::has_self) {
            PyObject* slice = PySlice_New(
                Type<typename impl::ArgTraits<typename Func::Self>::type>(),
                issubclass<Func::Self, impl::TypeTag>() ?
                    reinterpret_cast<PyObject*>(&PyType_Type) :
                    Py_None,
                ptr(rtype)
            );
            if (slice == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(ptr(result), 0, slice);
        } else {
            PyObject* slice = PySlice_New(
                Py_None,
                Py_None,
                ptr(rtype)
            );
            if (slice == nullptr) {
                Exception::from_python();
            }
            PyTuple_SET_ITEM(ptr(result), 0, slice);
        }

        constexpr size_t PosOnly = Func::has_posonly ? 
            std::min({Func::args_idx, Func::kw_idx, Func::kwargs_idx}) :
            Func::n;

        []<size_t... Is>(
            std::index_sequence<Is...>,
            PyObject* list
        ) {
            size_t offset = 1;
            (get<Is, PosOnly, Func::kwonly_idx>(list, offset), ...);
        }(std::make_index_sequence<Func::n>{}, ptr(result));
        return result;
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


/// TODO: all of these should be moved to their respective methods:
/// -   assert_matches() is needed in isinstance() + issubclass() to ensure
///     strict type safety.  Maybe also in the constructor, which can be
///     avoided using CTAD.
/// -   assert_satisfies() is needed in .overload()


struct TODO2 {

    template <size_t I, typename Container>
    static bool _matches(const Params<Container>& key) {
        using T = __cast__<std::remove_cvref_t<typename ArgTraits<at<I>>::type>>::type;
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
        using T = __cast__<std::remove_cvref_t<typename ArgTraits<at<I>>::type>>::type;

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
        using T = __cast__<std::remove_cvref_t<typename ArgTraits<at<I>>::type>>::type;

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
        using T = __cast__<std::remove_cvref_t<typename ArgTraits<at<I>>::type>>::type;

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

};




template <typename T, typename R, typename... A>
struct __isinstance__<T, Function<R(A...)>>                 : Returns<bool> {
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

    // template <typename T>
    // concept is_callable_any = 
    //     std::is_function_v<std::remove_pointer_t<std::decay_t<T>>> ||
    //     std::is_member_function_pointer_v<std::decay_t<T>> ||
    //     has_call_operator<T>;


template <typename T, typename R, typename... A>
struct __issubclass__<T, Function<R(A...)>>                 : Returns<bool> {
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


/* Call the function with the given arguments.  If the wrapped function is of the
coupled Python type, then this will be translated into a raw C++ call, bypassing
Python entirely. */
template <impl::inherits<impl::FunctionTag> Self, typename... Args>
    requires (std::remove_reference_t<Self>::bind<Args...>)
struct __call__<Self, Args...> : Returns<typename std::remove_reference_t<Self>::Return> {
    using Func = std::remove_reference_t<Self>;
    static Func::Return operator()(Self&& self, Args&&... args) {
        if (!self->overloads.data.empty()) {
            /// TODO: generate an overload key from the C++ arguments
            /// -> This can be implemented in Arguments<...>::Bind<...>::key()
            PyObject* overload = self->overloads.search(/* overload key */);
            if (overload) {
                return Func::call(overload, std::forward<Args>(args)...);
            }
        }
        return Func::call(self->defaults, self->func, std::forward<Args>(args)...);
    }
};


/// TODO: __getitem__, __contains__, __iter__, __len__, __bool__


template <typename F>
template <typename Self, typename Func>
    requires (
        !std::is_const_v<std::remove_reference_t<Self>> &&
        compatible<Func>
    )
void Interface<Function<F>>::overload(this Self&& self, const Function<Func>& func) {
    /// TODO: C++ side of function overloading
}


template <typename F>
template <typename T>
void Interface<Function<F>>::method(this const auto& self, Type<T>& type) {
    /// TODO: C++ side of method binding
}


template <typename F>
template <typename T>
void Interface<Function<F>>::classmethod(this const auto& self, Type<T>& type) {
    /// TODO: C++ side of method binding
}


template <typename F>
template <typename T>
void Interface<Function<F>>::staticmethod(this const auto& self, Type<T>& type) {
    /// TODO: C++ side of method binding
}


template <typename F>
template <typename T>
void Interface<Function<F>>::property(
    this const auto& self,
    Type<T>& type,
    /* setter */,
    /* deleter */
) {
    /// TODO: C++ side of method binding
}


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
            ptr(template_string<Name>())
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
            ptr(template_string<Name>())
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

    /// NOTE: the type returned by `std::mem_fn()` is implementation-defined, so we
    /// have to do some template magic to trick the compiler into deducing the correct
    /// type during template specializations.

    template <typename T>
    struct respecialize { static constexpr bool enable = false; };
    template <template <typename...> typename T, typename... Ts>
    struct respecialize<T<Ts...>> {
        static constexpr bool enable = true;
        template <typename... New>
        using type = T<New...>;
    };
    template <typename Sig>
    using std_mem_fn_type = respecialize<
        decltype(std::mem_fn(std::declval<void(Object::*)()>()))
    >::template type<Sig>;

};


#define NON_MEMBER_FUNC(IN, OUT) \
    template <typename R, typename... A> \
    struct __cast__<IN> : Returns<Function<OUT>> {};

#define MEMBER_FUNC(IN, OUT) \
    template <typename R, typename C, typename... A> \
    struct __cast__<IN> : Returns<Function<OUT>> {};

#define STD_MEM_FN(IN, OUT) \
    template <typename R, typename C, typename... A> \
    struct __cast__<impl::std_mem_fn_type<IN>> : Returns<Function<OUT>> {};


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


/////////////////////////
////    OPERATORS    ////
/////////////////////////


/// TODO: Object::operator()


}  // namespace py


#endif
